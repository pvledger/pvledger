#include "portability.hpp"
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iostream>
#include <sstream>
#ifdef _WIN32
#include "windows_runtime.hpp"
#else
#include <sys/utsname.h>
#endif

namespace pvl {
const std::vector<Resource> resources = {
    {"accounts/current", "pvl_accounts_current", "current_accounts", Resource::Write, "id_user"},
    {"accounts/savings", "pvl_accounts_savings", "savings_accounts", Resource::Write, "id_user"},
    {"accounts/credit", "pvl_accounts_credit", "credit_accounts", Resource::Write, "id_user"},
    {"income", "pvl_income", "income", Resource::Write, "id_account"},
    {"planned-income", "pvl_income_planned", "planned_income", Resource::Write, "id_account"},
    {"expenses", "pvl_expenses", "expenses", Resource::Write, "id_account"},
    {"planned-expenses", "pvl_expenses_planned", "planned_expenses", Resource::Write, "id_account"},
    {"recurring-expenses", "pvl_expenses_recurring", "recurring_expenses", Resource::Write, "id_account"},
    {"expense-categories", "pvl_expenses_categories", "categories", Resource::Write},
    {"debts", "pvl_debts", "debts", Resource::Write, "id_account"},
    {"taxes", "pvl_taxes", "taxes", Resource::Write, "id_account"},
    {"banks", "pvl_banks", "banks", Resource::Write},
    {"countries", "pvl_countries", "countries", Resource::Admin},
    {"currencies", "pvl_currencies", "currencies", Resource::Admin},
    {"users", "pvl_users", "users", Resource::Admin},
    {"roles", "pvl_user_roles", "roles", Resource::Read}
};
namespace {
constexpr const char* apiPrefix = "/api/v1";
constexpr const char* htmlType = "text/html; charset=utf-8";
constexpr std::pair<const char*, const char*> reportPages[] = {
    {"/reports/expense-categories", "expense_breakdown"},
    {"/reports/spending", "spending_timeline"},
    {"/reports/estimates", "estimates"},
    {"/reports/balances", "balance_timeline"},
    {"/reports/balance-projection", "balance_projection"},
    {"/reports/fund-distribution", "fund_distribution"}
};

bool starts(const std::string& value, const std::string& prefix) {
    return value.compare(0, prefix.size(), prefix) == 0;
}

Response data(const Value& value, int status = 200) {
    return {status, "application/json; charset=utf-8", json(value)};
}

Response redirect(const std::string& location) {
    Response response(303, "text/plain; charset=utf-8", "");
    response.location = location;
    return response;
}

Value single(const Value& rows) {
    if (rows.empty()) throw Error(404, "not_found");
    return rows[0];
}

std::string stringField(const Value& input, const std::string& name, std::size_t maximum, bool required = true) {
    const auto& value = input[name];
    if (!required && value.isNull()) return "";
    if (!value.isString()) throw Error(422, "invalid_input");
    const auto text = value.asString();
    if (text.size() > maximum || (required && text.find_first_not_of(" \t\r\n") == std::string::npos) ||
        text.find('\0') != std::string::npos) throw Error(422, "invalid_input");
    return text;
}

std::string decode(const std::string& value) {
    std::string decoded;
    for (std::size_t index = 0; index < value.size(); ++index) {
        char c = value[index];
        if (c == '+') c = ' ';
        else if (c == '%') {
            if (index + 2 >= value.size()) throw Error(400, "invalid_input");
            const auto digit = [](char d) {
                if (d >= '0' && d <= '9') return d - '0';
                if (d >= 'a' && d <= 'f') return d - 'a' + 10;
                if (d >= 'A' && d <= 'F') return d - 'A' + 10;
                throw Error(400, "invalid_input");
            };
            c = static_cast<char>(digit(value[index + 1]) * 16 + digit(value[index + 2]));
            index += 2;
        }
        if (c == '\0') throw Error(400, "invalid_input");
        decoded += c;
    }
    return decoded;
}

Value inputFor(const Request& request) {
    if (starts(request.contentType, "application/json")) return parseJson(request.body);
    if (!starts(request.contentType, "application/x-www-form-urlencoded")) throw Error(415, "invalid_input");
    Value result(Json::objectValue);
    std::stringstream fields(request.body);
    std::string pair;
    while (std::getline(fields, pair, '&')) {
        const auto separator = pair.find('=');
        if (separator == std::string::npos) throw Error(400, "invalid_input");
        const auto key = decode(pair.substr(0, separator));
        if (result.isMember(key)) throw Error(400, "invalid_input");
        result[key] = decode(pair.substr(separator + 1));
    }
    return result;
}

std::string cookieToken(const std::string& cookies) {
    std::stringstream fields(cookies);
    std::string pair;
    while (std::getline(fields, pair, ';')) {
        const auto start = pair.find_first_not_of(" \t");
        if (start != std::string::npos && pair.compare(start, 12, "pvl_session=") == 0)
            return pair.substr(start + 12);
    }
    return "";
}

void flatten(const YAML::Node& node, const std::string& prefix, Params& result) {
    if (node.IsScalar()) { result[prefix] = node.as<std::string>(); return; }
    if (!node.IsMap()) throw std::runtime_error("Locale must contain maps and scalar strings");
    for (const auto& entry : node) {
        const auto key = entry.first.as<std::string>();
        flatten(entry.second, prefix.empty() ? key : prefix + "." + key, result);
    }
}

bool contained(const fs::path& root, const fs::path& file) {
    if (!fs::is_regular_file(file)) return false;
    const auto relative = fs::canonical(file).lexically_relative(fs::canonical(root));
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

std::string render(const std::string& source, const Params& placeholders, const Params& raw = {}) {
    std::string output;
    std::size_t position = 0;
    while (position < source.size()) {
        const auto open = source.find("{{", position);
        output += source.substr(position, open - position);
        if (open == std::string::npos) break;
        const auto close = source.find("}}", open + 2);
        if (close == std::string::npos) throw std::logic_error("Malformed template");
        const auto key = source.substr(open + 2, close - open - 2);
        if (const auto match = raw.find(key); match != raw.end()) output += match->second;
        else if (const auto match = placeholders.find(key); match != placeholders.end()) output += escape(match->second);
        position = close + 2;
    }
    return output;
}

long long number(const Value& field) {
    if (field.isString()) return integer(field.asString());
    if ((field.type() != Json::intValue && field.type() != Json::uintValue) || !field.isInt64())
        throw Error(422, "invalid_input");
    return field.asInt64();
}

Json::Int64 queryInt(const Request& request, const std::string& key, long long fallback) {
    const auto found = request.query.find(key);
    return found == request.query.end() ? fallback : integer(found->second);
}

void checkTime(long long seconds) {
    if (seconds < -62135596800LL || seconds > 253402300799LL)
        throw Error(422, "invalid_input");
}

std::string ownerWhere(const Resource& resource, int role, Json::Int64 user) {
    if (role == 1 || !*resource.owner) return "";
    if (std::string(resource.owner) == "id_user") return "id_user=" + std::to_string(user);
    return "id_account IN (SELECT id FROM pvl_accounts_current WHERE id_user=" + std::to_string(user) + ")";
}

std::string where(const std::vector<std::string>& filters) {
    std::string result;
    for (const auto& filter : filters) {
        if (!filter.empty()) result += (result.empty() ? " WHERE " : " AND ") + filter;
    }
    return result;
}

Value fields(Repository& store, const Resource& resource) {
    auto list = store.query(std::string("PRAGMA table_info(") + resource.table + ")");
    const auto relations = store.query(std::string("PRAGMA foreign_key_list(") + resource.table + ")");
    for (auto& field : list) {
        const auto name = field["name"].asString();
        if (name == "id" || name == "is_admin") continue;
        for (const auto& relation : relations)
            if (relation["from"].asString() == name)
                for (const auto& target : resources)
                    if (relation["table"].asString() == target.table) field["resource"] = target.route;
    }
    return list;
}

Value checkedField(const std::string& name, const Value& value, const Value& field) {
    const auto type = field["type"].asString();
    if (value.isNull()) {
        if (field["notnull"].asInt() || name == "username" || name == "password")
            throw Error(422, "invalid_input");
        return value;
    }
    if (type == "INTEGER") {
        const auto n = number(value);
        if ((starts(name, "id_") && n < 1) ||
            (name == "id_role" && n > 3) ||
            (name == "status" && (n < 0 || n > 3)))
            throw Error(422, "invalid_input");
        if (name == "date" || name == "billing_date") checkTime(n);
        return Json::Int64(n);
    }
    if (type == "REAL") {
        if (!value.isNumeric() || !std::isfinite(value.asDouble())) throw Error(422, "invalid_input");
        return value.asDouble();
    }
    Value wrapper;
    wrapper[name] = value;
    const bool required = field["notnull"].asInt() || name == "username" || name == "password";
    const auto text = stringField(wrapper, name, name == "password" ? 1024 : 4096, required);
    if (name == "password") {
        if (text.size() < 12) throw Error(422, "password_length");
        return passwordHash(text);
    }
    return text;
}

std::string availableColumns(const Resource& resource) {
    const std::string table = resource.table;
    if (table == "pvl_users")
        return "id,username,firstname,lastname,displayname,id_role";
    if (std::string(resource.owner) == "id_user" || table == "pvl_expenses" || table == "pvl_expenses_planned")
        return "*,(SELECT COALESCE(iso,name) FROM pvl_currencies WHERE id=pvl_entry.id_currency) AS currency";
    if (std::string(resource.owner) == "id_account")
        return "*,(SELECT COALESCE(iso,name) FROM pvl_currencies WHERE id=(SELECT a.id_currency FROM pvl_accounts_current a "
               "WHERE a.id=pvl_entry.id_account)) AS account_currency";
    return "*";
}

Value browse(Repository& store, const Resource& resource, const Request& request, int role, Json::Int64 user,
             Json::Int64 id) {
    const std::string table = resource.table;
    std::vector<std::string> filters{ownerWhere(resource, role, user)};
    std::vector<Value> arguments;
    const auto schema = fields(store, resource);
    if (id) { filters.emplace_back("id=?"); arguments.emplace_back(id); }
    std::string ordering = "id";
    Json::Int64 page = 1;
    Json::Int64 size = 50;
    if (!id) {
        const auto has = [&](const std::string& key) {
            return std::any_of(schema.begin(), schema.end(), [&key](const Value& field) {
                return field["name"].asString() == key;
            });
        };
        for (const auto& [key, expression] : Params{{"id_account", "id_account=?"}, {"status", "status=?"},
                                                   {"from", "date>=?"}, {"to", "date<=?"}}) {
            if (!request.query.count(key)) continue;
            const bool date = key == "from" || key == "to";
            if (!has(date ? "date" : key)) throw Error(422, "invalid_input");
            const auto value = queryInt(request, key, 0);
            if (date) checkTime(value);
            if (key == "id_account" && value <= 0) throw Error(422, "invalid_input");
            filters.push_back(expression);
            arguments.emplace_back(value);
        }
        if (const auto sort = request.query.find("sort"); sort != request.query.end()) {
            const bool descending = starts(sort->second, "-");
            const auto column = descending ? sort->second.substr(1) : sort->second;
            if (!has(column) || column == "password" || column == "is_admin") throw Error(422, "invalid_input");
            ordering = column + (descending ? " DESC" : " ASC");
        } else if (table == "pvl_income" || table == "pvl_income_planned") ordering = "date DESC";
        page = queryInt(request, "page", 1);
        size = queryInt(request, "page_size", 50);
        if (page < 1 || page > 1000000 || size < 1 || size > 100) throw Error(422, "invalid_input");
    }
    const auto condition = where(filters);
    const auto from = " FROM " + table + " AS pvl_entry";
    const auto count = id ? Value{} : single(store.query("SELECT count(*) AS count" + from + condition, arguments))["count"];
    arguments.emplace_back(size);
    arguments.emplace_back((page - 1) * size);
    auto rows = store.query("SELECT " + availableColumns(resource) + from + condition +
                            " ORDER BY " + ordering + ",id LIMIT ? OFFSET ?", arguments);
    if (id) return single(rows);
    Value result;
    result["items"] = std::move(rows);
    result["page"] = page;
    result["page_size"] = size;
    result["total"] = count;
    result["fields"] = schema;
    result["writable"] = role == 1 ? resource.access != Resource::Read : role == 2 && resource.access == Resource::Write;
    return result;
}

Value writeResource(Repository& db, const Resource& resource, const Request& request, int role, Json::Int64 user,
                    Json::Int64 id, bool& deleted) {
    if (resource.access == Resource::Read || role == 3 || (resource.access == Resource::Admin && role != 1))
        throw Error(403, "forbidden");
    const bool create = request.method == "POST";
    deleted = request.method == "DELETE";
    if ((create && id) || (!create && !id) || (!create && !deleted && request.method != "PATCH"))
        throw Error(405, "method");
    Transaction transaction(db);
    const std::string table = resource.table;
    if (id) browse(db, resource, request, role, user, id);
    if (deleted) {
        if (table == "pvl_users" && id == user) throw Error(409, "self_admin");
        db.execute("DELETE FROM " + table + " WHERE id=?", {id});
        transaction.commit();
        return {};
    }
    auto input = inputFor(request);
    if (input.empty()) throw Error(422, "invalid_input");
    if (role != 1 && std::string(resource.owner) == "id_user") {
        if (input.isMember("id_user") && number(input["id_user"]) != user) throw Error(403, "forbidden");
        if (create) input["id_user"] = user;
    }
    if (role != 1) {
        for (const std::string key : {"id_account", "id_current_account"}) {
            if (input.isMember(key) && db.query("SELECT id FROM pvl_accounts_current WHERE id=? AND id_user=?",
                                               {Json::Int64(number(input[key])), user}).empty())
                throw Error(422, "invalid_reference");
        }
    }
    const auto schema = fields(db, resource);
    if (create) {
        for (const auto& field : schema) {
            const auto name = field["name"].asString();
            if (name != "id" && (field["notnull"].asInt() || name == "username" || name == "password") &&
                !input.isMember(name)) throw Error(422, "invalid_input");
        }
    }
    std::vector<Value> arguments;
    std::string keys, placeholders;
    for (const auto& name : input.getMemberNames()) {
        if (name == "id" || name == "is_admin") throw Error(422, "invalid_input");
        const auto field = std::find_if(schema.begin(), schema.end(), [&name](const Value& item) {
            return item["name"].asString() == name;
        });
        if (field == schema.end()) throw Error(422, "invalid_input");
        if (name == "id_role" && id == user && number(input[name]) != 1) throw Error(409, "self_admin");
        if (!keys.empty()) { keys += ','; placeholders += ','; }
        keys += create ? name : name + "=?";
        placeholders += '?';
        arguments.push_back(checkedField(name, input[name], *field));
    }
    if (create) id = db.execute("INSERT INTO " + table + " (" + keys + ") VALUES (" + placeholders + ")", arguments);
    else {
        arguments.emplace_back(id);
        db.execute("UPDATE " + table + " SET " + keys + " WHERE id=?", arguments);
    }
    if (table == "pvl_users") db.execute("UPDATE pvl_users SET is_admin=(id_role=1) WHERE id=?", {id});
    auto result = browse(db, resource, request, role, user, id);
    transaction.commit();
    return result;
}
}

Application::Application(Config settings) : config(std::move(settings)), setupToken(token()) {
    const auto schema = config.assets / "db/pvledger.sql";
    store = openDatabase(config.database);
    prepareDatabase(*store, schema);
    for (const auto& entry : fs::directory_iterator(config.languages)) {
        const auto path = entry.path();
        const auto key = path.stem().u8string();
        if (path.extension() != ".yaml" || !identifier(key) || !contained(config.languages, path)) continue;
        Params pack;
        flatten(YAML::Load(readFile(path)), "", pack);
        if (pack["meta.locale"] != key || pack["meta.name"].empty())
            throw std::runtime_error("Invalid locale metadata for " + key);
        localeNames[key] = pack["meta.name"];
        locales.emplace(key, std::move(pack));
    }
    if (!locales.count("en_US")) throw std::runtime_error("Required en_US locale is missing");
    for (const auto& entry : fs::directory_iterator(config.themes)) {
        const auto name = entry.path().filename().u8string();
        const auto css = entry.path() / "style.css";
        if (identifier(name) && contained(config.themes, css)) themes[name] = css;
    }
    if (!themes.count("default")) throw std::runtime_error("Required default theme is missing");
    layout = readFile(config.assets / "templates/layout.html");
    setupForm = readFile(config.assets / "templates/setup.html");
    loginForm = readFile(config.assets / "templates/login.html");
    discoverPlugins();
    if (ready()) {
        Transaction transaction(*store);
        registerPlugins(*store);
        transaction.commit();
    }
}

void Application::registerPlugins(Repository& db) {
    for (const auto& [id, plugin] : plugins)
        db.execute("INSERT INTO pvl_plugins(name,path,enabled) SELECT ?,?,0 "
                       "WHERE NOT EXISTS(SELECT 1 FROM pvl_plugins WHERE path=?)", {plugin.name, id, id});
}

void Application::discoverPlugins() {
    for (const auto& entry : fs::directory_iterator(config.plugins)) {
        const auto manifest = entry.path() / "plugin.yaml";
        if (!contained(config.plugins, manifest)) continue;
        try {
            const auto yaml = YAML::Load(readFile(manifest));
            const auto id = yaml["id"].as<std::string>();
            const auto name = yaml["name"].as<std::string>();
            const auto label = yaml["menu_label"].as<std::string>();
            const auto content = yaml["text"].as<std::string>();
            if (!identifier(id) || name.empty() || label.empty() || content.empty() ||
                yaml["api_version"].as<int>() != 1 || yaml["entrypoint"] || plugins.count(id))
                throw std::runtime_error("Invalid plugin manifest");
            plugins.emplace(id, Plugin{id, name, label, content});
        } catch (const std::exception&) {
            std::cerr << "Ignoring an invalid or incompatible plugin manifest\n";
        }
    }
}

Response Application::handle(const Request& request) {
    std::lock_guard<std::mutex> guard(mutex);
    try {
        return dispatch(request);
    } catch (const std::exception& error) {
        const auto* known = dynamic_cast<const Error*>(&error);
        const int status = known ? known->status : 500;
        const std::string key = known ? known->what() : "internal";
        if (!known) std::cerr << "Request failed with an internal error\n";
        const auto& english = locales.at("en_US");
        const auto fallback = english.find("errors." + key);
        std::string message = fallback == english.end() ? "PVLedger" : fallback->second;
        try {
            const auto settings = store->query("SELECT lang FROM pvl_settings LIMIT 1");
            if (!settings.empty()) {
                const auto pack = locales.find(settings[0]["lang"].asString());
                if (pack != locales.end() && pack->second.count("errors." + key))
                    message = pack->second.at("errors." + key);
            }
        } catch (const std::exception&) {}
        if (starts(request.path, "/api/")) {
            Value result;
            result["error"]["code"] = key;
            result["error"]["message"] = message;
            return data(result, status);
        }
        return {status, htmlType,
                render(layout, {{"locale", "en-US"}, {"title", message}},
                       {{"content", "<h1>" + escape(message) + "</h1><p><a href=\"/\">PVLedger</a></p>"}})};
    }
}

Response Application::dispatch(const Request& request) {
    const auto& path = request.path;
    const bool get = request.method == "GET";
    const bool post = request.method == "POST";
    const bool api = starts(path, std::string(apiPrefix) + "/");
    const auto preferences = store->query("SELECT * FROM pvl_settings LIMIT 1");
    const bool ready = !preferences.empty();
    const Value settings = ready ? preferences[0] : Value{};
    auto locale = settings["lang"].asString();
    if (!ready && request.query.count("lang")) locale = request.query.at("lang");
    if (!locales.count(locale)) locale = "en_US";
    auto translations = locales.at("en_US");
    for (const auto& [key, value] : locales.at(locale)) translations[key] = value;
    const auto tr = [&translations](const std::string& key) -> std::string {
        const auto text = translations.find(key);
        return text == translations.end() ? key : text->second;
    };
    const auto page = [&](const std::string& title, const std::string& content, const std::string& csrf = "") {
        const auto instance = settings["instance_name"].asString();
        auto values = translations;
        values["locale"] = locale;
        std::replace(values["locale"].begin(), values["locale"].end(), '_', '-');
        values["title"] = title;
        values["brand"] = instance.empty() ? "PVLedger" : instance;
        values["csrf"] = csrf;
        values["layout_class"] = csrf.empty() ? "public-page" : "signed-in";
        return Response(200, htmlType, render(layout, values, {{"content", content}}));
    };
    const auto cookie = [&](const std::string& value) {
        return "pvl_session=" + value + "; Path=/; HttpOnly; SameSite=Strict" +
               (config.secureCookie ? "; Secure" : "") + (value.empty() ? "; Max-Age=0" : "");
    };
    if (get && (path == "/assets/js/menu.js" || path == "/assets/js/app.js" || path == "/assets/js/workflows.js" || path == "/assets/js/import-file.js"))
        return {200, "text/javascript; charset=utf-8", readFile(config.assets / path.substr(8))};
    if (get && path == "/assets/theme.css") {
        auto selected = settings["theme"].asString();
        if (!themes.count(selected)) selected = "default";
        const auto css = themes.at(selected);
        if (!contained(config.themes, css)) throw Error(404, "not_found");
        if (selected == "dark") {
            const auto base = themes.at("default");
            if (!contained(config.themes, base)) throw Error(404, "not_found");
            return {200, "text/css; charset=utf-8", readFile(base) + "\n" + readFile(css)};
        }
        return {200, "text/css; charset=utf-8", readFile(css)};
    }
    if (get && (path == "/api/v1/system/health" || path == "/api/v1/system/version")) {
        Value result;
        if (path == "/api/v1/system/health") result["status"] = ready ? "ready" : "setup";
        else result["version"] = PVL_VERSION;
        return data(result);
    }
    const auto currencyOptions = [&](const std::string& selected) {
        std::string options;
        for (const auto& row : store->query("SELECT id,iso,name FROM pvl_currencies ORDER BY iso,name")) {
            const auto id = row["id"].asString();
            const auto iso = row["iso"].asString();
            options += "<option value=\"" + escape(id) + "\"" + (id == selected ? " selected" : "") +
                       ">" + escape(iso.empty() ? row["name"].asString() : iso + " · " + row["name"].asString()) + "</option>";
        }
        return options;
    };
    const auto selectOptions = [](const auto& values, const std::string& selected, const auto& label) {
        std::string options;
        for (const auto& [key, value] : values)
            options += "<option value=\"" + escape(key) + "\"" + (key == selected ? " selected" : "") +
                       ">" + escape(label(key, value)) + "</option>";
        return options;
    };
    const auto languageOptions = [&] {
        return selectOptions(localeNames, locale, [](const auto&, const auto& name) { return name; });
    };
    const auto themeOptions = [&] {
        const auto selected = settings["theme"].asString();
        return selectOptions(themes, selected.empty() ? "default" : selected,
                             [&](const auto& name, const auto&) {
                                 return name == "default" || name == "dark" ? tr("theme_" + name) : name;
                             });
    };
    const auto validatePreferences = [&](const Value& input) {
        const auto language = stringField(input, "lang", 80);
        const auto theme = stringField(input, "theme", 80);
        if (!locales.count(language) || !themes.count(theme)) throw Error(422, "invalid_input");
        const auto name = stringField(input, "instance_name", 160, false);
        Value currency;
        if (!input["default_currency"].isNull() && input["default_currency"] != "") {
            const auto id = number(input["default_currency"]);
            if (id <= 0) throw Error(422, "invalid_reference");
            currency = Json::Int64(id);
        }
        return std::vector<Value>{name, language, currency, theme};
    };
    const auto restoreForm = [&](const std::string& csrf, bool firstRun) {
        auto values = translations;
        values["restore_token"] = csrf;
        values["restore_url"] = firstRun ? "/api/v1/setup/restore" : "/api/v1/settings/restore";
        values["restore_hint"] = tr(firstRun ? "setup_restore_hint" : "settings_restore_hint");
        values["restore_confirmation"] = firstRun ? "" : tr("confirm_restore");
        return render(readFile(config.assets / "templates/restore.html"), values);
    };
    const auto restore = [&]() -> Response {
        if (!post) throw Error(405, "method");
        if (request.contentType != "application/octet-stream" && request.contentType != "application/vnd.sqlite3")
            throw Error(415, "invalid_input");
        if (request.checkRestoreAccess) return {204, "text/plain", ""};
        restoreBackup(config, request.body, !ready, [&](Repository& db) {
            registerPlugins(db);
            if (!ready && config.missing) config.save();
        });
        sessions.clear();
        loginFailures = 0;
        auto response = Response(204, "text/plain", "");
        response.cookie = cookie("");
        return response;
    };
    if (!ready) {
        if (get && path == "/") return redirect("/setup");
        if (path == "/api/v1/setup/restore") {
            if (request.csrf != setupToken) throw Error(403, "csrf");
            return restore();
        }
        if (path != "/setup") throw Error(404, "not_found");
        if (get) {
            auto values = translations;
            values["setup_token"] = setupToken;
            values["locale_id"] = locale;
            values["database_path"] = config.database.u8string();
            values["port"] = std::to_string(config.port);
            return page(tr("setup"), render(setupForm, values,
                        {{"languages", languageOptions()}, {"themes", themeOptions()}, {"currencies", currencyOptions("")},
                         {"restore_form", restoreForm(setupToken, true)}}));
        }
        if (!post) throw Error(405, "method");
        const auto input = inputFor(request);
        if (stringField(input, "csrf", 64) != setupToken) throw Error(403, "csrf");
        auto preferencesInput = validatePreferences(input);
        const auto username = stringField(input, "username", 120);
        const auto password = stringField(input, "password", 1024);
        if (password.size() < 12) throw Error(422, "password_length");
        Transaction transaction(*store);
        if (!store->query("SELECT id FROM pvl_settings LIMIT 1").empty() ||
            !store->query("SELECT id FROM pvl_users LIMIT 1").empty()) throw Error(409, "constraint");
        store->execute("INSERT INTO pvl_users(username,password,displayname,firstname,lastname,is_admin,id_role) "
                       "VALUES(?,?,?,?,?,1,1)", {username, passwordHash(password),
                           stringField(input, "displayname", 160, false), stringField(input, "firstname", 160, false),
                           stringField(input, "lastname", 160, false)});
        preferencesInput.emplace_back(currentSchemaVersion);
        store->execute("INSERT INTO pvl_settings(id,instance_name,lang,default_currency,theme,db_version) "
                       "VALUES(1,?,?,?,?,?)", preferencesInput);
        registerPlugins(*store);
        if (config.missing) config.save();
        transaction.commit();
        return redirect("/login");
    }
    if (path == "/setup" || starts(path, "/setup/") || path == "/api/v1/setup/restore") throw Error(404, "not_found");
    const auto now = Clock::now();
    for (auto it = sessions.begin(); it != sessions.end();) {
        if (it->second.expires <= now) it = sessions.erase(it);
        else ++it;
    }
    const bool login = path == "/login" || path == "/api/v1/auth/login";
    if (login) {
        if (get && !api)
            return page(tr("login"), render(loginForm, translations));
        if (!post) throw Error(405, "method");
        // Require a custom request header for browser logins to prevent cross-origin login CSRF.
        if (request.csrf != "login") throw Error(403, "csrf");
        if (now - loginWindow >= std::chrono::minutes(5)) {
            loginWindow = now;
            loginFailures = 0;
        }
        if (loginFailures >= 12) throw Error(429, "rate_limit");
        const auto input = inputFor(request);
        const auto username = stringField(input, "username", 120);
        const auto password = stringField(input, "password", 1024);
        const auto users = store->query("SELECT id,password,id_role FROM pvl_users WHERE username=?", {username});
        const auto stored = users.empty() ? "" : users[0]["password"].asString();
        const bool verified = verifyPassword(password, stored);
        if (!verified || users.empty() || users[0]["id_role"].asInt() < 1 || users[0]["id_role"].asInt() > 3) {
            ++loginFailures;
            throw Error(401, "login_failed");
        }
        const auto user = users[0]["id"].asInt64();
        if (stored.size() == 64)
            store->execute("UPDATE pvl_users SET password=? WHERE id=? AND password=?", {passwordHash(password), user, stored});
        sessions.erase(cookieToken(request.cookie));
        const auto key = token();
        const auto csrf = token();
        sessions[key] = {user, csrf, now + std::chrono::minutes(30), {}, {}};
        Value result;
        result["csrf"] = csrf;
        auto response = api ? data(result) : redirect("/");
        response.cookie = cookie(key);
        return response;
    }
    const auto sessionId = cookieToken(request.cookie);
    const auto session = sessions.find(sessionId);
    if (session == sessions.end()) {
        if (get && !api) return redirect("/login");
        throw Error(401, "unauthorized");
    }
    const auto user = session->second.user;
    auto users = store->query("SELECT id,username,displayname,id_role FROM pvl_users WHERE id=?", {Json::Int64(user)});
    if (users.empty()) {
        sessions.erase(session);
        throw Error(401, "unauthorized");
    }
    auto actor = users[0];
    const auto role = actor["id_role"].asInt();
    if (role < 1 || role > 3) throw Error(403, "forbidden");
    const auto csrf = session->second.csrf;
    if (!get && request.csrf != csrf) throw Error(403, "csrf");
    session->second.expires = now + std::chrono::minutes(30);

    if (path == "/api/v1/auth/me" && get) {
        actor["csrf"] = csrf;
        actor["default_currency"] = settings["default_currency"];
        return data(actor);
    }
    if (path == "/api/v1/auth/logout" && post) {
        sessions.erase(session);
        auto response = data(Value(Json::objectValue));
        response.cookie = cookie("");
        return response;
    }
    const auto requireAdmin = [&] { if (role != 1) throw Error(403, "forbidden"); };
    const auto shell = [&](const std::string& heading, const std::string& route, const std::string& fragment = "") {
        return page(heading, "<h1>" + escape(heading) + "</h1>" + fragment +
                    "<div id=\"workspace\" data-route=\"" + escape(route) + "\" aria-live=\"polite\"></div>", csrf);
    };
    if (get && starts(path, "/api/v1/i18n/")) {
        const auto selected = locales.find(path.substr(std::string("/api/v1/i18n/").size()));
        if (selected == locales.end()) throw Error(404, "not_found");
        Value result(Json::objectValue);
        for (const auto& [key, text] : locales.at("en_US")) result[key] = text;
        for (const auto& [key, text] : selected->second) result[key] = text;
        return data(result);
    }
    const auto installed = [&](bool enabledOnly) {
        Value result(Json::arrayValue);
        for (const auto& row : store->query("SELECT id,name,path,enabled FROM pvl_plugins ORDER BY id")) {
            const auto found = plugins.find(row["path"].asString());
            if (enabledOnly && (found == plugins.end() || row["enabled"].asInt() != 1)) continue;
            auto plugin = row;
            plugin["available"] = found != plugins.end();
            if (found != plugins.end()) plugin["label"] = found->second.label;
            result.append(plugin);
        }
        return result;
    };
    if (get && path == "/api/v1/ui/menu") {
        Value result(Json::arrayValue);
        const auto add = [&](const std::string& route, const std::string& label, const std::string& group = "") {
            Value item;
            item["href"] = route;
            item["label"] = label;
            item["group"] = group;
            result.append(item);
        };
        add("/", tr("dashboard"));
        for (const auto& resource : resources) {
            const std::string route = resource.route;
            if (role != 1 && (route == "users" || route == "roles")) continue;
            std::string group;
            if (starts(route, "accounts/")) group = tr("accounts");
            else if (route.find("income") != std::string::npos) group = tr("income_group");
            else if (route.find("expense") != std::string::npos) group = tr("expense_group");
            else if (route == "countries" || route == "currencies" || route == "banks") group = tr("reference");
            else if (route == "users" || route == "roles") group = tr("administration");
            add("/" + route, tr(resource.label), group);
        }
        for (const auto& [route, label] : reportPages) add(route, tr(label), tr("reports"));
        if (role != 3) add("/data/import", tr("import_file"), tr("data_tools"));
        add("/data/export", tr("export_file"), tr("data_tools"));
        if (role == 1) {
            add("/settings", tr("settings"), tr("administration"));
            add("/plugins", tr("plugins"), tr("administration"));
        }
        for (const auto& plugin : installed(true))
            add("/plugins/" + plugin["path"].asString(), plugin["label"].asString(), tr("plugins"));
        add("/about", tr("about"));
        return data(result);
    }
    if (get && starts(path, "/api/v1/lookups/")) {
        const auto pageNumber = queryInt(request, "page", 1);
        if (pageNumber < 1 || pageNumber > 1000000) throw Error(422, "invalid_input");
        return data(lookup(*store, path.substr(16), role, user, static_cast<int>(pageNumber)));
    }
    if (get && (path == "/data/import" || path == "/data/export")) {
        const bool importing = path == "/data/import";
        if (importing && role == 3) throw Error(403, "forbidden");
        return shell(tr(importing ? "import_file" : "export_file"), path.substr(1));
    }
    if (get && starts(path, "/reports/")) {
        for (const auto& [route, label] : reportPages)
            if (path == route) return shell(tr(label), path.substr(1));
        throw Error(404, "not_found");
    }
    if (get && starts(path, "/api/v1/reports/"))
        return data(report(*store, path.substr(16), request.query, role, user));
    if (post && (path == "/api/v1/export/preview" || path == "/api/v1/export/download"))
        return exportData(*store, inputFor(request), path == "/api/v1/export/download", role, user);
    if (post && (path == "/api/v1/import/preview" || path == "/api/v1/import/commit")) {
        if (role == 3) throw Error(403, "forbidden");
        const auto input = inputFor(request);
        auto& job = session->second;
        if (path == "/api/v1/import/preview") {
            job.preview = previewExpenses(*store, input, role, user);
            job.preview["token"] = token();
            job.previewExpires = now + std::chrono::minutes(10);
            return data(job.preview);
        }
        if (!input["confirm"].isBool() || !input["confirm"].asBool() || job.preview.isNull() ||
            job.previewExpires <= now || input["token"] != job.preview["token"]) throw Error(409, "preview_needed");
        auto result = commitExpenses(*store, job.preview, input["rows"], role, user);
        job.preview = {};
        return data(result);
    }
    if (get && path == "/about") {
        auto values = translations;
#ifdef _WIN32
        SYSTEM_INFO system{};
        GetNativeSystemInfo(&system);
        char build[32]{};
        DWORD size = sizeof(build);
        const auto status = RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                                         "CurrentBuildNumber", RRF_RT_REG_SZ, nullptr, build, &size);
        values["server_os"] = status == ERROR_SUCCESS ? std::string("Windows build ") + build : "Windows";
        values["server_architecture"] = system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "arm64" :
            system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x86_64" : "x86";
#else
        struct utsname system{};
        const bool hasSystem = uname(&system) == 0;
        values["server_os"] = hasSystem ? std::string(system.sysname) + " " + system.release : "—";
        values["server_architecture"] = hasSystem ? system.machine : "—";
#endif
        values["sqlite_runtime"] = single(store->query("SELECT sqlite_version() AS version"))["version"].asString();
        values["db_schema"] = settings["db_version"].asString();
        values["app_version"] = PVL_VERSION;
        return page(tr("about"), render(readFile(config.assets / "templates/about.html"), values), csrf);
    }
    if (get && starts(path, "/plugins/")) {
        const auto id = path.substr(9);
        const auto plugin = plugins.find(id);
        if (plugin == plugins.end() || store->query("SELECT id FROM pvl_plugins WHERE path=? AND enabled=1", {id}).empty())
            throw Error(404, "not_found");
        return page(plugin->second.label, "<h1>" + escape(plugin->second.label) + "</h1><p>" +
                    escape(plugin->second.content) + "</p>", csrf);
    }
    if (path == "/plugins" || path == "/api/v1/plugins" || starts(path, "/api/v1/plugins/")) {
        requireAdmin();
        if (get && !api) return shell(tr("plugins"), "plugins");
        if (get && path == "/api/v1/plugins") return data(installed(false));
        if (post && starts(path, "/api/v1/plugins/")) {
            const auto suffix = path.substr(16);
            const auto separator = suffix.find('/');
            if (separator == std::string::npos) throw Error(404, "not_found");
            const auto id = Json::Int64(integer(suffix.substr(0, separator)));
            const auto operation = suffix.substr(separator + 1);
            if (operation != "enable" && operation != "disable") throw Error(404, "not_found");
            const auto plugin = single(store->query("SELECT path FROM pvl_plugins WHERE id=?", {id}));
            if (operation == "enable" && !plugins.count(plugin["path"].asString())) throw Error(409, "constraint");
            store->execute("UPDATE pvl_plugins SET enabled=? WHERE id=?", {operation == "enable" ? 1 : 0, id});
            return data(installed(false));
        }
        throw Error(405, "method");
    }
    if (path == "/api/v1/settings/backup" || path == "/api/v1/settings/restore") {
        requireAdmin();
        if (path == "/api/v1/settings/restore") return restore();
        if (!get) throw Error(405, "method");
        Response response(200, "application/vnd.sqlite3", downloadBackup(*store));
        response.filename = "pvledger-backup.sqlite";
        return response;
    }
    if (path == "/settings" || path == "/api/v1/settings") {
        requireAdmin();
        if (get && !api) {
            const auto templateSource = readFile(config.assets / "templates/settings.html");
            auto values = translations;
            values["current_instance"] = settings["instance_name"].asString();
            return page(tr("settings"), render(templateSource, values,
                        {{"languages", languageOptions()}, {"themes", themeOptions()},
                         {"currencies", currencyOptions(settings["default_currency"].asString())},
                         {"restore_form", restoreForm(csrf, false)}}), csrf);
        }
        if (get) return data(settings);
        if (request.method != "PATCH") throw Error(405, "method");
        auto input = inputFor(request);
        if (input.empty()) throw Error(422, "invalid_input");
        for (const std::string field : {"instance_name", "lang", "default_currency", "theme"})
            if (!input.isMember(field)) input[field] = settings[field];
        if (input.size() != 4) throw Error(422, "invalid_input");
        store->execute("UPDATE pvl_settings SET instance_name=?,lang=?,default_currency=?,theme=?", validatePreferences(input));
        return data(single(store->query("SELECT * FROM pvl_settings")));
    }
    if (get && path == "/") return shell(tr("dashboard"), "dashboard");
    if (get && path == "/api/v1/dashboard") {
        const auto totals = [&](const std::string& table, const std::string& owner) {
            return store->query("SELECT currency.id, currency.iso AS currency, currency.name, SUM(entry.amount) AS amount "
                                "FROM " + table + " entry JOIN pvl_currencies currency ON currency.id=entry.id_currency" +
                                where({owner}) + " GROUP BY currency.id ORDER BY currency.iso,currency.id");
        };
        const auto visibility = role == 1 ? "" : "id_user=" + std::to_string(user);
        Value result;
        result["current_accounts"] = totals("pvl_accounts_current", visibility);
        result["savings_accounts"] = totals("pvl_accounts_savings", visibility);
        result["credit_accounts"] = totals("pvl_accounts_credit", visibility);
        result["debts"] = store->query("SELECT currency.id,currency.iso AS currency,currency.name,SUM(entry.amount) AS amount "
            "FROM pvl_debts entry JOIN pvl_accounts_current account ON account.id=entry.id_account "
            "JOIN pvl_currencies currency ON currency.id=account.id_currency" +
            where({role == 1 ? "" : "account.id_user=" + std::to_string(user)}) +
            " GROUP BY currency.id ORDER BY currency.iso,currency.id");
        result["recent_expenses"] = store->query("SELECT entry.id,entry.name,entry.amount,entry.date,currency.iso AS currency "
            "FROM pvl_expenses entry JOIN pvl_accounts_current account ON account.id=entry.id_account "
            "JOIN pvl_currencies currency ON currency.id=entry.id_currency" +
            where({role == 1 ? "" : "account.id_user=" + std::to_string(user)}) + " ORDER BY entry.date DESC,entry.id DESC LIMIT 8");
        return data(result);
    }
    for (const auto& resource : resources) {
        const auto base = std::string(api ? apiPrefix : "") + "/" + resource.route;
        if (path != base && !starts(path, base + "/")) continue;
        if (std::string(resource.route) == "users" || std::string(resource.route) == "roles") requireAdmin();
        Json::Int64 id = 0;
        bool history = false;
        std::string route = resource.route;
        if (path != base) {
            auto tail = path.substr(base.size() + 1);
            const auto end = tail.find('/');
            if (end != std::string::npos) {
                if (tail.substr(end) != "/history" ||
                    (route != "accounts/current" && route != "accounts/savings")) throw Error(404, "not_found");
                history = true;
                tail.resize(end);
            }
            id = integer(tail);
            if (id < 1) throw Error(404, "not_found");
            route += "/" + std::to_string(id);
        }
        if (history) {
            if (!get) throw Error(405, "method");
            const auto account = browse(*store, resource, request, role, user, id);
            const auto currency = single(store->query("SELECT iso,name FROM pvl_currencies WHERE id=?", {account["id_currency"]}));
            if (!api) return shell(tr("history"), route + "/history");
            std::vector<Value> args{id};
            std::vector<std::string> filters{"id_account=?"};
            for (const auto& [key, op] : Params{{"from", ">="}, {"to", "<="}}) {
                if (!request.query.count(key)) continue;
                const auto bound = queryInt(request, key, 0);
                checkTime(bound);
                args.emplace_back(bound);
                filters.emplace_back("date" + op + "?");
            }
            const std::string table = std::string(resource.route) == "accounts/current" ?
                                      "pvl_account_history_current" : "pvl_account_history_savings";
            const auto pageNumber = queryInt(request, "page", 1);
            if (pageNumber < 1 || pageNumber > 1000000) throw Error(422, "invalid_input");
            const auto condition = where(filters);
            Value result;
            result["total"] = single(store->query("SELECT count(*) AS count FROM " + table + condition, args))["count"];
            args.emplace_back((pageNumber - 1) * 50);
            result["items"] = store->query("SELECT id,id_account,date,amount FROM " + table + condition +
                                           " ORDER BY date DESC,id DESC LIMIT 50 OFFSET ?", args);
            result["page"] = pageNumber;
            result["page_size"] = 50;
            result["currency"] = currency;
            return data(result);
        }
        if (!api) {
            if (!get) throw Error(405, "method");
            if (id) browse(*store, resource, request, role, user, id);
            return shell(tr(resource.label), route);
        }
        if (get) return data(browse(*store, resource, request, role, user, id));
        bool deleted = false;
        auto result = writeResource(*store, resource, request, role, user, id, deleted);
        if (deleted) return {204, "application/json", ""};
        return data(result, post ? 201 : 200);
    }
    throw Error(404, "not_found");
}
}
