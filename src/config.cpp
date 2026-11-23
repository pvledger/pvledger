#include "pvledger.hpp"

#include <yaml-cpp/yaml.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <charconv>
#include <cctype>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace pvl {
std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + path.string());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) throw std::runtime_error("Cannot read " + path.string());
    return buffer.str();
}

Config Config::load(const fs::path& path) {
    Config result;
    result.file = fs::absolute(path).lexically_normal();
    std::error_code executableError;
    const auto executable = fs::read_symlink("/proc/self/exe", executableError);
    const auto installedAssets = (executable.parent_path() / PVL_DATA).lexically_normal();
    result.assets = !executableError && fs::exists(installedAssets / "templates/layout.html") ?
                    installedAssets : fs::path(PVL_SOURCE);
    result.languages = result.assets / "lang";
    result.plugins = fs::exists(result.assets / "plugins") ? result.assets / "plugins" : result.assets / "src/plugins";
    result.themes = result.assets / "themes";
    const auto base = result.file.parent_path();
    result.database = base / "data/pvledger.sqlite";
    result.missing = !fs::exists(result.file);
    if (result.missing) {
        if (!fs::is_directory(base) || access(base.c_str(), W_OK) != 0)
            throw std::runtime_error("Configuration directory must exist and be writable");
        return result;
    }
    try {
        const auto yaml = YAML::LoadFile(result.file.string());
        const auto app = yaml["app"];
        const auto database = yaml["database"];
        const auto paths = yaml["paths"];
        if (database["engine"].as<std::string>() != "sqlite")
            throw std::runtime_error("database.engine must be sqlite");
        const auto resolve = [&base](const YAML::Node& value) {
            const auto pathValue = value.as<std::string>();
            if (pathValue.empty()) throw std::runtime_error("Configuration paths must be nonempty");
            return (base / pathValue).lexically_normal();
        };
        result.database = resolve(database["sqlite"]["path"]);
        result.host = app["host"].as<std::string>("127.0.0.1");
        result.port = app["port"].as<int>();
        result.secureCookie = app["secure_cookie"].as<bool>(false);
        result.languages = resolve(paths["languages"]);
        result.plugins = resolve(paths["plugins"]);
        result.themes = resolve(paths["themes"]);
        if (result.port < 1 || result.port > 65535 || result.host.empty())
            throw std::runtime_error("app.host and app.port must specify a listening address and port (1–65535)");
    } catch (const YAML::Exception&) {
        throw std::runtime_error("Invalid configuration YAML or missing required keys");
    }
    for (const auto& directory : {result.languages, result.plugins, result.themes})
        if (!fs::is_directory(directory)) throw std::runtime_error("Missing directory: " + directory.string());
    return result;
}

void Config::save() const {
    YAML::Node yaml;
    yaml["app"]["host"] = host;
    yaml["app"]["port"] = port;
    yaml["app"]["secure_cookie"] = secureCookie;
    yaml["database"]["engine"] = "sqlite";
    yaml["database"]["sqlite"]["path"] = database.string();
    yaml["paths"]["languages"] = languages.string();
    yaml["paths"]["plugins"] = plugins.string();
    yaml["paths"]["themes"] = themes.string();
    const auto temporary = file.string() + "." + token();
    try {
        std::ofstream output(temporary);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << yaml << '\n';
        output.close();
        fs::rename(temporary, file);
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}

std::string json(const Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    return Json::writeString(builder, value);
}

Value parseJson(const std::string& input) {
    Json::CharReaderBuilder builder;
    builder["rejectDupKeys"] = true;
    builder["failIfExtra"] = true;
    builder["allowComments"] = false;
    Value result;
    std::string error;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(input.data(), input.data() + input.size(), &result, &error) || !result.isObject())
        throw Error(400, "invalid_input");
    return result;
}

std::string escape(const std::string& value) {
    std::string result;
    for (const char c : value) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default: result += c;
        }
    }
    return result;
}

bool identifier(const std::string& text) {
    return !text.empty() && text.size() <= 80 &&
        text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") == std::string::npos &&
        text != "." && text != "..";
}

long long integer(const std::string& text) {
    long long number = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size())
        throw Error(422, "invalid_input");
    return number;
}

static std::string hex(const unsigned char* bytes, std::size_t size) {
    static const std::string digits = "0123456789abcdef";
    std::string result;
    for (std::size_t i = 0; i < size; ++i) {
        result += digits[bytes[i] >> 4];
        result += digits[bytes[i] & 15];
    }
    return result;
}

std::string token() {
    unsigned char bytes[32];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) throw std::runtime_error("Random generator unavailable");
    return hex(bytes, sizeof(bytes));
}

static std::string derive(const std::string& password, const std::string& salt) {
    unsigned char digest[32];
    if (PKCS5_PBKDF2_HMAC(password.data(), password.size(),
            reinterpret_cast<const unsigned char*>(salt.data()), salt.size(), 600000, EVP_sha256(),
            sizeof(digest), digest) != 1) throw std::runtime_error("Password derivation failed");
    return hex(digest, sizeof(digest));
}

std::string passwordHash(const std::string& password) {
    const auto salt = token();
    return "pbkdf2-sha256$" + salt + "$" + derive(password, salt);
}

bool verifyPassword(const std::string& password, const std::string& stored) {
    constexpr auto prefix = "pbkdf2-sha256$";
    constexpr std::size_t offset = 14;
    std::string actual, expected;
    if (stored.size() == offset + 129 && stored.compare(0, offset, prefix) == 0 && stored[offset + 64] == '$') {
        actual = derive(password, stored.substr(offset, 64));
        expected = stored.substr(offset + 65);
    } else if (stored.size() == 64) {
        unsigned char digest[32];
        if (EVP_Digest(password.data(), password.size(), digest, nullptr, EVP_sha256(), nullptr) != 1)
            throw std::runtime_error("Password verification failed");
        actual = hex(digest, sizeof(digest));
        expected = stored;
    } else {
        actual = derive(password, std::string(64, '0'));
        expected = std::string(64, '0');
    }
    return CRYPTO_memcmp(actual.data(), expected.data(), expected.size()) == 0;
}
}
