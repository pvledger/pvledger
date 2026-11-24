#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <json/json.h>

namespace pvl {
namespace fs = std::filesystem;
using Value = Json::Value;
using Params = std::map<std::string, std::string>;
inline constexpr char currentSchemaVersion[] = "0.7.2";
inline constexpr int backupLimit = 64 * 1024 * 1024;

struct Error : std::runtime_error {
    int status;
    Error(int status, const std::string& key) : std::runtime_error(key), status(status) {}
};

struct Config {
    fs::path file, database, languages, plugins, themes, assets;
    std::string host = "127.0.0.1";
    int port = 8080;
    bool secureCookie = false;
    bool missing = false;
    static Config load(const fs::path& path);
    void save() const;
};

std::string readFile(const fs::path& path);
std::string json(const Value& value);
Value parseJson(const std::string& input);
std::string escape(const std::string& value);
bool identifier(const std::string& text);
long long integer(const std::string& text);
std::string token();
std::string passwordHash(const std::string& password);
bool verifyPassword(const std::string& password, const std::string& stored);

class Repository {
public:
    virtual ~Repository() = default;
    virtual Value query(const std::string& sql, const std::vector<Value>& args = {}) = 0;
    virtual long long execute(const std::string& sql, const std::vector<Value>& args = {}) = 0;
    virtual void begin() = 0;
    virtual void commit() = 0;
    virtual void rollback() noexcept = 0;
};

class Transaction {
    Repository& store;
    bool committed = false;
public:
    explicit Transaction(Repository& store) : store(store) { store.begin(); }
    ~Transaction() { if (!committed) store.rollback(); }
    void commit() { store.commit(); committed = true; }
};

std::unique_ptr<Repository> openDatabase(const fs::path& path);
bool initialized(Repository& store);
void prepareDatabase(Repository& store, const fs::path& schema);
std::string downloadBackup(Repository& store);
void restoreBackup(const Config& config, const std::string& bytes, bool setup,
                   const std::function<void(Repository&)>& finish);

struct Request {
    std::string method, path, body, cookie, csrf, contentType;
    Params query;
    bool checkRestoreAccess = false;
};
struct Response {
    int status;
    std::string type, body, cookie, location, filename;
    Response(int status, std::string type, std::string body)
        : status(status), type(std::move(type)), body(std::move(body)) {}
};

class Application {
    using Clock = std::chrono::steady_clock;
    struct Session { long long user; std::string csrf; Clock::time_point expires; Value preview; Clock::time_point previewExpires; };
    Config config;
    std::unique_ptr<Repository> store;
    std::mutex mutex;
    std::map<std::string, Session> sessions;
    std::map<std::string, Params> locales;
    std::map<std::string, std::string> localeNames;
    std::map<std::string, fs::path> themes;
    struct Plugin { std::string id, name, label, content; };
    std::map<std::string, Plugin> plugins;
    std::string layout, setupForm, loginForm, setupToken;
    Clock::time_point loginWindow = Clock::now();
    unsigned loginFailures = 0;

    void discoverPlugins();
    void registerPlugins(Repository& db);
    Response dispatch(const Request& request);
public:
    explicit Application(Config config);
    bool ready() { return initialized(*store); }
    Response handle(const Request& request);
};
}
