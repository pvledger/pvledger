#include "pvledger.hpp"
#include "oatpp/core/base/Environment.hpp"
#include "oatpp/network/Server.hpp"
#include "oatpp/network/tcp/server/ConnectionProvider.hpp"
#include "oatpp/web/server/HttpConnectionHandler.hpp"
#include "oatpp/web/protocol/http/outgoing/ResponseFactory.hpp"
#include <csignal>
#include <cstdlib>
#include <iostream>
#ifdef _WIN32
#include "windows_runtime.hpp"
#include <cstdint>
#include <cwchar>
#else
#include <pthread.h>
#include <sys/stat.h>
#endif

namespace {
namespace http = oatpp::web::protocol::http;
using oatpp::web::server::HttpRequestHandler;
using oatpp::web::server::HttpRouter;

std::string standard(const oatpp::String& value) {
    return value ? *value : std::string{};
}

const char* reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 303: return "See Other";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 422: return "Unprocessable Content";
        case 429: return "Too Many Requests";
        case 503: return "Service Unavailable";
        default: return "Internal Server Error";
    }
}

class Controller final : public HttpRequestHandler {
    pvl::Application& application;
    const bool localHostOnly;
    const std::string port;

    std::shared_ptr<OutgoingResponse> send(const pvl::Response& response) {
        auto outgoing = http::outgoing::ResponseFactory::createResponse(
            http::Status(response.status, reason(response.status)), oatpp::String(response.body));
        outgoing->putHeader("Content-Type", oatpp::String(response.type));
        outgoing->putHeader("Cache-Control", "no-store");
        outgoing->putHeader("X-Content-Type-Options", "nosniff");
        outgoing->putHeader("Referrer-Policy", "no-referrer");
        outgoing->putHeader("Content-Security-Policy",
            "default-src 'none'; script-src 'self'; connect-src 'self'; style-src 'self'; "
            "form-action 'self'; frame-ancestors 'none'; base-uri 'none'");
        outgoing->putHeader("Connection", "close");
        if (!response.filename.empty())
            outgoing->putHeader("Content-Disposition", oatpp::String("attachment; filename=\"" + response.filename + "\""));
        if (!response.cookie.empty()) outgoing->putHeader("Set-Cookie", oatpp::String(response.cookie));
        if (!response.location.empty()) outgoing->putHeader("Location", oatpp::String(response.location));
        return outgoing;
    }

public:
    Controller(pvl::Application& application, bool localHostOnly, int port)
        : application(application), localHostOnly(localHostOnly), port(std::to_string(port)) {}

    std::shared_ptr<OutgoingResponse> handle(const std::shared_ptr<IncomingRequest>& incoming) override {
        if (localHostOnly) {
            const auto host = standard(incoming->getHeader("Host"));
            bool authorized = false;
            for (const std::string local : {"localhost", "127.0.0.1"})
                if (host == local || host == local + ":" + port) authorized = true;
            if (!authorized) return send({403, "text/plain", "Invalid Host header"});
        }
        pvl::Request request;
        const auto& line = incoming->getStartingLine();
        request.method = standard(line.method.toString());
        const auto target = standard(line.path.toString());
        request.path = target.substr(0, target.find('?'));
        if (request.path.empty()) request.path = "/";
        request.cookie = standard(incoming->getHeader("Cookie"));
        request.csrf = standard(incoming->getHeader("X-CSRF-Token"));
        request.contentType = standard(incoming->getHeader("Content-Type"));
        for (const std::string key : {"page", "page_size", "sort", "id_account", "status", "from", "to", "lang", "id_currency", "account_type"}) {
            const auto value = incoming->getQueryParameter(oatpp::String(key));
            if (value) request.query.emplace(key, *value);
        }
        // The local API accepts fixed-length request bodies only.
        const auto length = standard(incoming->getHeader("Content-Length"));
        if (incoming->getHeader("Transfer-Encoding"))
            return send({400, "text/plain", "A fixed Content-Length is required"});
        if (!length.empty()) {
            try {
                const auto bytes = pvl::integer(length);
                const bool restore = request.method == "POST" && (request.path == "/api/v1/setup/restore" ||
                                                                 request.path == "/api/v1/settings/restore");
                const int limit = restore ? pvl::backupLimit : request.path == "/api/v1/import/preview" ? 1024 * 1024 : 65536;
                if (bytes > limit) return send({413, "text/plain", "Request body exceeds the limit"});
                if (bytes < 0) return send({400, "text/plain", "Invalid Content-Length"});
                if (restore && bytes) {
                    // Authorize before buffering a file. Recheck after upload in case setup or the session changed.
                    request.checkRestoreAccess = true;
                    const auto access = application.handle(request);
                    if (access.status != 204) return send(access);
                    request.checkRestoreAccess = false;
                }
                if (bytes && request.method != "GET") {
                    request.body = standard(incoming->readBodyToString());
                    if (request.body.size() != static_cast<std::size_t>(bytes))
                        return send({400, "text/plain", "Incomplete request body"});
                }
            } catch (const std::exception&) {
                return send({400, "text/plain", "Invalid request body"});
            }
        }
        return send(application.handle(request));
    }
};

struct Environment {
    Environment() { oatpp::base::Environment::init(); }
    ~Environment() { oatpp::base::Environment::destroy(); }
};

#ifdef _WIN32
HANDLE consoleStop = nullptr;

BOOL WINAPI stopFromConsole(DWORD control) {
    if (control != CTRL_C_EVENT && control != CTRL_BREAK_EVENT) return FALSE;
    return SetEvent(consoleStop);
}

void notifyHelper(HANDLE pipe, const std::string& message) {
    if (!pipe) return;
    DWORD written;
    WriteFile(pipe, message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
}

void run(pvl::Config config, HANDLE stop, HANDLE startup) {
#else
void run(pvl::Config config) {
#endif
    Environment environment;
    pvl::Application application(config);
    const auto bind = application.ready() ? config.host : "127.0.0.1";
    const bool local = bind == "127.0.0.1" || bind == "localhost";
    const auto* containerBind = std::getenv("PVLEDGER_CONTAINER_BIND");
    const std::string listenAddress = local && containerBind && *containerBind ? containerBind : bind;
    auto router = HttpRouter::createShared();
    auto controller = std::make_shared<Controller>(application, local && (!application.ready() || !config.secureCookie),
                                                  config.port);
    for (const auto* method : {"GET", "POST", "PATCH", "DELETE", "PUT", "OPTIONS", "HEAD"}) {
        router->route(method, "/", controller);
        router->route(method, "/*", controller);
    }
    auto handler = oatpp::web::server::HttpConnectionHandler::createShared(router);
    auto provider = oatpp::network::tcp::server::ConnectionProvider::createShared(
        {oatpp::String(listenAddress), static_cast<v_uint16>(config.port), oatpp::network::Address::IP_4});

#ifndef _WIN32
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0)
        throw std::runtime_error("Cannot configure shutdown signals");
#endif
    oatpp::network::Server server(provider, handler);
    server.run(true);
    std::cout << "PVLedger listening on " << listenAddress << ':' << config.port << std::endl;
#ifdef _WIN32
    notifyHelper(startup, "R" + listenAddress + ":" + std::to_string(config.port) + "\n");
    WaitForSingleObject(stop, INFINITE);
#else
    int signal = 0;
    sigwait(&signals, &signal);
#endif
    server.stop();
    provider->stop();
    handler->stop();
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    const std::wstring option = argc > 1 ? argv[1] : L"";
    const auto help = L"--help", version = L"--version", configOption = L"-c";
    const bool managed = argc == 6 && std::wstring(argv[3]) == pvl::win::helperHandles;
#else
int main(int argc, char* argv[]) {
    const std::string option = argc > 1 ? argv[1] : "";
    const auto help = "--help", version = "--version", configOption = "-c";
    constexpr bool managed = false;
#endif
    if (argc == 2 && option == help) {
        std::cout << "Usage: pvledger -c <config.yaml>\n"
                     "       pvledger --help | --version\n"
                     "  -c <file>  Load configuration or start local setup.\n";
        return 0;
    }
    if (argc == 2 && option == version) {
        std::cout << "PVLedger " PVL_VERSION "\n";
        return 0;
    }
    if ((!managed && argc != 3) || option != configOption) {
        std::cerr << "Expected -c <config.yaml>. Use --help for usage.\n";
        return 2;
    }
#ifdef _WIN32
    const auto handle = [](const wchar_t* value) -> HANDLE {
        return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::wcstoull(value, nullptr, 10)));
    };
    pvl::win::Handle stop(managed ? handle(argv[4]) : CreateEventW(nullptr, TRUE, FALSE, nullptr));
    pvl::win::Handle startup(managed ? handle(argv[5]) : nullptr);
#else
    umask(0077);
#endif
    try {
#ifdef _WIN32
        pvl::win::check(bool(stop), "Cannot create shutdown event");
        if (!managed) {
            consoleStop = stop.get();
            pvl::win::check(SetConsoleCtrlHandler(stopFromConsole, TRUE), "Cannot configure console shutdown");
        }
        run(pvl::Config::load(argv[2]), stop.get(), startup.get());
#else
        run(pvl::Config::load(argv[2]));
#endif
        return 0;
    } catch (const std::exception& error) {
#ifdef _WIN32
        notifyHelper(startup.get(), std::string("E") + std::string(error.what()).substr(0, 2048) + "\n");
#endif
        std::cerr << "PVLedger startup failed: " << error.what() << '\n';
        return 1;
    }
}
