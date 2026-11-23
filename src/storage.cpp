#include "pvledger.hpp"
#include <sqlite3.h>
#include <ctime>
#include <sys/stat.h>

namespace pvl {
namespace {
class Sqlite final : public Repository {
    struct Close { void operator()(sqlite3* db) const { sqlite3_close(db); } };
    using Handle = std::unique_ptr<sqlite3, Close>;
    using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
    Handle db;

    void check(int code) const {
        if (code == SQLITE_OK || code == SQLITE_DONE || code == SQLITE_ROW) return;
        if ((code & 255) == SQLITE_CONSTRAINT) throw Error(409, "constraint");
        if (code == SQLITE_BUSY || code == SQLITE_LOCKED) throw Error(503, "busy");
        throw std::runtime_error("SQLite operation failed (" + std::to_string(code) + ")");
    }

    Statement prepare(const std::string& sql, const std::vector<Value>& args) {
        sqlite3_stmt* raw = nullptr;
        const int code = sqlite3_prepare_v2(db.get(), sql.c_str(), -1, &raw, nullptr);
        Statement statement(raw, sqlite3_finalize);
        check(code);
        if (sqlite3_bind_parameter_count(raw) != static_cast<int>(args.size()))
            throw std::logic_error("Wrong SQL binding count");
        for (std::size_t index = 0; index < args.size(); ++index) {
            const auto& arg = args[index];
            const int position = static_cast<int>(index + 1);
            if (arg.isNull()) check(sqlite3_bind_null(raw, position));
            else if (arg.type() == Json::intValue || arg.type() == Json::uintValue)
                check(sqlite3_bind_int64(raw, position, arg.asInt64()));
            else if (arg.isDouble()) check(sqlite3_bind_double(raw, position, arg.asDouble()));
            else {
                const auto text = arg.asString();
                check(sqlite3_bind_text(raw, position, text.data(), text.size(), SQLITE_TRANSIENT));
            }
        }
        return statement;
    }

public:
    explicit Sqlite(const fs::path& path) {
        sqlite3* raw = nullptr;
        const int code = sqlite3_open_v2(path.c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        db.reset(raw);
        check(code);
        sqlite3_busy_timeout(raw, 5000);
        execute("PRAGMA foreign_keys=ON");
    }

    Value query(const std::string& sql, const std::vector<Value>& args = {}) override {
        auto statement = prepare(sql, args);
        auto* raw = statement.get();
        Value rows(Json::arrayValue);
        int code;
        while ((code = sqlite3_step(raw)) == SQLITE_ROW) {
            Value row(Json::objectValue);
            for (int i = 0; i < sqlite3_column_count(raw); ++i) {
                auto& value = row[sqlite3_column_name(raw, i)];
                switch (sqlite3_column_type(raw, i)) {
                    case SQLITE_INTEGER: value = Json::Int64(sqlite3_column_int64(raw, i)); break;
                    case SQLITE_FLOAT: value = sqlite3_column_double(raw, i); break;
                    case SQLITE_TEXT:
                        value = std::string(reinterpret_cast<const char*>(sqlite3_column_text(raw, i)),
                                            sqlite3_column_bytes(raw, i));
                        break;
                }
            }
            rows.append(std::move(row));
        }
        check(code);
        return rows;
    }

    long long execute(const std::string& sql, const std::vector<Value>& args = {}) override {
        auto statement = prepare(sql, args);
        check(sqlite3_step(statement.get()));
        return sqlite3_last_insert_rowid(db.get());
    }

    void script(const std::string& input) {
        check(sqlite3_exec(db.get(), input.c_str(), nullptr, nullptr, nullptr));
    }

    void backup(const fs::path& path) {
        Sqlite destination(path);
        auto* operation = sqlite3_backup_init(destination.db.get(), "main", db.get(), "main");
        if (!operation) throw std::runtime_error("Cannot initialize migration backup");
        const int step = sqlite3_backup_step(operation, -1);
        const int finish = sqlite3_backup_finish(operation);
        if (step != SQLITE_DONE || finish != SQLITE_OK)
            throw std::runtime_error("Cannot complete migration backup");
    }

    void begin() override { execute("BEGIN IMMEDIATE"); }
    void commit() override { execute("COMMIT"); }
    void rollback() noexcept override { sqlite3_exec(db.get(), "ROLLBACK", nullptr, nullptr, nullptr); }
};

std::string qualified(const std::string& table) {
    if (!identifier(table)) throw std::runtime_error("Unexpected schema identifier");
    return "'" + table + "'";
}

Value tables(Repository& db) {
    return db.query("SELECT name,sql FROM sqlite_schema WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name");
}

bool hasColumn(Repository& db, const std::string& table, const std::string& name) {
    for (const auto& col : db.query("PRAGMA table_info(" + qualified(table) + ")"))
        if (col["name"].asString() == name) return true;
    return false;
}

Value columns(Repository& db, const std::string& table) {
    Value fields(Json::objectValue);
    for (auto col : db.query("PRAGMA table_info(" + qualified(table) + ")")) {
        col.removeMember("cid");
        fields[col["name"].asString()] = col;
    }
    return fields;
}

void validate(Repository& db, Repository& canonical, bool undatedIncome = false) {
    const auto actual = tables(db);
    const auto expected = tables(canonical);
    if (actual.size() != expected.size() || expected.size() != 20)
        throw std::runtime_error("Unexpected SQLite table inventory");
    for (const auto& definition : expected) {
        const auto table = definition["name"].asString();
        const auto pragma = "PRAGMA foreign_key_list(" + qualified(table) + ")";
        auto expectedColumns = columns(canonical, table);
        if (undatedIncome && (table == "pvl_income" || table == "pvl_income_planned"))
            expectedColumns.removeMember("date");
        if (columns(db, table) != expectedColumns || db.query(pragma) != canonical.query(pragma))
            throw std::runtime_error("Unexpected schema for " + table);
    }
    const auto indexes = db.query("PRAGMA index_list(pvl_currencies)");
    bool uniqueIso = false;
    for (const auto& index : indexes)
        if (index["name"].asString() == "ux_pvl_currencies_iso" && index["unique"].asInt() == 1)
            uniqueIso = db.query("PRAGMA index_info(ux_pvl_currencies_iso)") ==
                        canonical.query("PRAGMA index_info(ux_pvl_currencies_iso)");
    if (!uniqueIso || !db.query("PRAGMA foreign_key_check").empty())
        throw std::runtime_error("SQLite index or foreign-key integrity check failed");
}

void upgrade(Sqlite& db, Repository& canonical, std::string version) {
    const auto create = [&](const std::string& name) {
        db.execute(canonical.query("SELECT sql FROM sqlite_schema WHERE name=?", {name})[0]["sql"].asString());
    };
    if (version == "0.5") {
        create("pvl_account_history_current");
        create("pvl_account_history_savings");
        version = "0.6.1";
    }
    if (version == "0.6") {
        if (!db.query("SELECT id FROM pvl_account_history_current WHERE id_account IS NULL LIMIT 1").empty())
            throw std::runtime_error("Migration blocked: reconcile NULL current-history account links");
        db.execute("ALTER TABLE pvl_account_history_current RENAME TO legacy_history");
        create("pvl_account_history_current");
        db.execute("INSERT INTO pvl_account_history_current (id,id_account,date,amount) "
                   "SELECT id,id_account,date,amount FROM legacy_history");
        db.execute("UPDATE sqlite_sequence SET seq=max(seq,coalesce((SELECT seq FROM sqlite_sequence "
                   "WHERE name='legacy_history'),0)) WHERE name='pvl_account_history_current'");
        db.execute("INSERT INTO sqlite_sequence(name,seq) SELECT 'pvl_account_history_current',seq "
                   "FROM sqlite_sequence WHERE name='legacy_history' AND NOT EXISTS "
                   "(SELECT 1 FROM sqlite_sequence WHERE name='pvl_account_history_current')");
        db.execute("DROP TABLE legacy_history");
        version = "0.6.1";
    }
    if (version == "0.6.1") {
        for (const std::string name : {"pvl_income", "pvl_income_planned"}) {
            auto ddl = canonical.query("SELECT sql FROM sqlite_schema WHERE name=?", {name})[0]["sql"].asString();
            const auto date = ddl.find(", date INTEGER NOT NULL");
            if (date == std::string::npos) throw std::runtime_error("Missing canonical income date");
            ddl.erase(date, std::string(", date INTEGER NOT NULL").size());
            db.execute(ddl);
        }
        version = "0.7";
    }
    if (version == "0.7") {
        for (const std::string name : {"pvl_income", "pvl_income_planned"}) {
            if (hasColumn(db, name, "date")) throw std::runtime_error("Unexpected v0.7 income schema");
            if (!db.query("SELECT id FROM " + name + " LIMIT 1").empty())
                throw std::runtime_error("Migration blocked: populated v0.7 income requires reviewed timestamp backfill");
            db.execute("ALTER TABLE " + name + " ADD COLUMN date INTEGER NOT NULL");
        }
        version = "0.7.1";
    }
    if (version != "0.7.1") throw std::runtime_error("Unsupported SQLite schema version");
}
}

std::unique_ptr<Repository> openDatabase(const fs::path& path) {
    fs::create_directories(path.parent_path());
    return std::make_unique<Sqlite>(path);
}

bool initialized(Repository& db) {
    return !db.query("SELECT id FROM pvl_settings LIMIT 1").empty() &&
           !db.query("SELECT id FROM pvl_users WHERE id_role=1 AND username IS NOT NULL "
                     "AND username<>'' AND (length(password)=64 OR password LIKE 'pbkdf2-sha256$%') LIMIT 1").empty();
}

void prepareDatabase(Repository& store, const fs::path& schema) {
    auto& db = dynamic_cast<Sqlite&>(store);
    Sqlite canonical(":memory:");
    const auto script = readFile(schema);
    canonical.script(script);
    if (tables(db).empty()) {
        Transaction transaction(db);
        if (tables(db).empty()) {
            for (const auto& definition : tables(canonical)) db.execute(definition["sql"].asString());
            db.execute("CREATE UNIQUE INDEX ux_pvl_currencies_iso ON pvl_currencies(iso)");
            for (const auto& row : canonical.query("SELECT * FROM pvl_countries"))
                db.execute("INSERT INTO pvl_countries(id,name,iso) VALUES(?,?,?)", {row["id"],row["name"],row["iso"]});
            for (const auto& row : canonical.query("SELECT * FROM pvl_currencies"))
                db.execute("INSERT INTO pvl_currencies(id,name,description,iso) VALUES(?,?,?,?)",
                           {row["id"],row["name"],row["description"],row["iso"]});
            for (const auto& row : canonical.query("SELECT * FROM pvl_user_roles"))
                db.execute("INSERT INTO pvl_user_roles(id,name,description) VALUES(?,?,?)",
                           {row["id"],row["name"],row["description"]});
            validate(db, canonical);
        }
        transaction.commit();
    }
    const auto settings = db.query("SELECT db_version FROM pvl_settings LIMIT 2");
    if (settings.size() > 1) throw std::runtime_error("Expected a singleton settings row");
    const bool unclaimed = settings.empty();
    if (unclaimed && !db.query("SELECT id FROM pvl_users LIMIT 1").empty())
        throw std::runtime_error("Existing users found without instance settings");
    const bool seededV07 = unclaimed && !hasColumn(db, "pvl_income", "date") &&
                           !hasColumn(db, "pvl_income_planned", "date");
    if (seededV07) validate(db, canonical, true);
    const auto version = unclaimed ? (seededV07 ? "0.7" : "0.7.1") : settings[0]["db_version"].asString();
    if (version == "0.7.1") {
        validate(db, canonical);
        if (!settings.empty() && !initialized(db))
            throw std::runtime_error("Instance has settings but no usable administrator");
        return;
    }
    if (version != "0.5" && version != "0.6" && version != "0.6.1" && version != "0.7")
        throw std::runtime_error("Unsupported SQLite schema version");
    const auto database = db.query("PRAGMA database_list")[0]["file"].asString();
    db.backup(database + ".pre-" + version + "." + token().substr(0, 12) + ".backup");
    Transaction transaction(db);
    if (db.query("SELECT db_version FROM pvl_settings LIMIT 2") != settings)
        throw std::runtime_error("Database version changed during migration");
    if (seededV07) {
        if (!db.query("SELECT id FROM pvl_users LIMIT 1").empty())
            throw std::runtime_error("Existing users found without instance settings");
        validate(db, canonical, true);
    } else if (!initialized(db)) {
        throw std::runtime_error("Existing installation lacks a usable administrator");
    }
    upgrade(db, canonical, version);
    validate(db, canonical);
    db.execute("UPDATE pvl_settings SET db_version='0.7.1'");
    transaction.commit();
}
}
