#include "pvledger.hpp"
#include <sqlite3.h>
#include <ctime>
#include <fstream>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace pvl {
namespace {
struct BackupDirectory {
    fs::path path = fs::temp_directory_path() / ("pvledger-" + token());
    BackupDirectory() {
#ifdef _WIN32
        if (!fs::create_directory(path)) throw std::runtime_error("Cannot create backup directory");
#else
        if (::mkdir(path.c_str(), 0700) != 0) throw std::runtime_error("Cannot create backup directory");
#endif
    }
    ~BackupDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

class Sqlite final : public Repository {
    struct Close { void operator()(sqlite3* db) const { sqlite3_close(db); } };
    using Handle = std::unique_ptr<sqlite3, Close>;
    using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
    Handle db;

    void check(int code) const {
        if (code == SQLITE_OK || code == SQLITE_DONE || code == SQLITE_ROW) return;
        if (sqlite3_extended_errcode(db.get()) == SQLITE_CONSTRAINT_TRIGGER) {
            const std::string message = sqlite3_errmsg(db.get());
            for (const auto key : {"invalid_balance", "posting_amount_required", "posting_currency_mismatch", "opening_balance_required"})
                if (message == key) throw Error(422, key);
        }
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
        const int code = sqlite3_open_v2(path.u8string().c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
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
        if (!operation) throw std::runtime_error("Cannot initialize SQLite backup");
        const int step = sqlite3_backup_step(operation, -1);
        const int finish = sqlite3_backup_finish(operation);
        if (step != SQLITE_DONE || finish != SQLITE_OK)
            throw std::runtime_error("Cannot complete SQLite backup");
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

Value postingTriggers(Repository& db) {
    return db.query("SELECT name,sql FROM sqlite_schema WHERE type='trigger' AND name IN "
                    "('pvl_current_balance_history','pvl_savings_balance_history','pvl_post_expense','pvl_post_income') ORDER BY name");
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
        if (undatedIncome && table == "pvl_income") {
            expectedColumns["status"] = expectedColumns["name"];
            expectedColumns["status"]["name"] = "status";
            expectedColumns["status"]["type"] = "INTEGER";
        }
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
            if (name == "pvl_income") {
                // This historical schema still has a required actual-income status.
                ddl.insert(ddl.find(", description TEXT"), ", status INTEGER NOT NULL");
            }
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
    if (version == "0.7.1") {
        for (const auto& trigger : postingTriggers(canonical)) db.execute(trigger["sql"].asString());
        version = "0.7.2";
    }
    if (version == "0.7.2") {
        if (postingTriggers(db) != postingTriggers(canonical))
            throw std::runtime_error("Unexpected SQLite balance posting rules");
        db.execute("ALTER TABLE pvl_income DROP COLUMN status");
        version = currentSchemaVersion;
    }
    if (version != currentSchemaVersion) throw std::runtime_error("Unsupported SQLite schema version");
}
}

std::unique_ptr<Repository> openDatabase(const fs::path& path) {
    fs::create_directories(path.parent_path());
    return std::make_unique<Sqlite>(path);
}

bool initialized(Repository& db) {
    return !db.query("SELECT id FROM pvl_settings LIMIT 1").empty() &&
           !db.query("SELECT id FROM pvl_users WHERE id_role=1 AND username IS NOT NULL "
                     "AND username<>'' AND typeof(password)='text' AND ("
                     "(length(password)=64 AND password NOT GLOB '*[^0-9a-f]*') OR "
                     "(length(password)=143 AND substr(password,1,14)='pbkdf2-sha256$' "
                     "AND substr(password,79,1)='$' AND substr(password,80) NOT GLOB '*[^0-9a-f]*')) LIMIT 1").empty();
}

void prepareDatabase(Repository& store, const fs::path& schema) {
    auto& db = dynamic_cast<Sqlite&>(store);
    Sqlite canonical(":memory:");
    const auto script = readFile(schema);
    canonical.script(script);
    const auto triggers = postingTriggers(canonical);
    if (triggers.size() != 4) throw std::runtime_error("Missing balance posting rules in schema");
    const auto validateCurrent = [&](bool claimed) {
        validate(db, canonical);
        if (postingTriggers(db) != triggers) throw std::runtime_error("Unexpected SQLite balance posting rules");
        if (claimed && !initialized(db))
            throw std::runtime_error("Instance has settings but no usable administrator");
    };
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
            for (const auto& trigger : triggers) db.execute(trigger["sql"].asString());
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
    auto version = settings.empty() ? std::string{} : settings[0]["db_version"].asString();
    if (seededV07) version = "0.7";
    else if (unclaimed)
        version = hasColumn(db, "pvl_income", "status") ?
                  (postingTriggers(db).empty() ? "0.7.1" : "0.7.2") : currentSchemaVersion;
    if (version == currentSchemaVersion) {
        validateCurrent(!unclaimed);
        return;
    }
    if (version != "0.5" && version != "0.6" && version != "0.6.1" && version != "0.7" &&
        version != "0.7.1" && version != "0.7.2")
        throw std::runtime_error("Unsupported SQLite schema version");
    const auto database = db.query("PRAGMA database_list")[0]["file"].asString();
    db.backup(database + ".pre-" + version + "." + token().substr(0, 12) + ".backup");
    Transaction transaction(db);
    const auto lockedSettings = db.query("SELECT db_version FROM pvl_settings LIMIT 2");
    if ((lockedSettings.size() == 1 && lockedSettings[0]["db_version"] == currentSchemaVersion) ||
        (lockedSettings.empty() && unclaimed && !hasColumn(db, "pvl_income", "status"))) {
        validateCurrent(!lockedSettings.empty());
        transaction.commit();
        return;
    }
    if (lockedSettings != settings)
        throw std::runtime_error("Database version changed during migration");
    if (seededV07) {
        if (!db.query("SELECT id FROM pvl_users LIMIT 1").empty())
            throw std::runtime_error("Existing users found without instance settings");
        validate(db, canonical, true);
    } else if (!unclaimed && !initialized(db)) {
        throw std::runtime_error("Existing installation lacks a usable administrator");
    }
    upgrade(db, canonical, version);
    validateCurrent(!unclaimed);
    db.execute("UPDATE pvl_settings SET db_version=?", {currentSchemaVersion});
    transaction.commit();
}

static Json::Int64 databaseBytes(Repository& db) {
    return db.query("SELECT page_size * page_count AS bytes FROM pragma_page_size, pragma_page_count")[0]["bytes"].asInt64();
}

std::string downloadBackup(Repository& store) {
    if (databaseBytes(store) > backupLimit) throw Error(413, "backup_limit");
    BackupDirectory temporary;
    const auto file = temporary.path / "ledger.sqlite";
    dynamic_cast<Sqlite&>(store).backup(file);
    if (fs::file_size(file) > backupLimit) throw Error(413, "backup_limit");
    return readFile(file);
}

void restoreBackup(const Config& config, const std::string& bytes, bool setup,
                   const std::function<void(Repository&)>& finish) {
    if (bytes.size() > backupLimit) throw Error(413, "backup_limit");
    if (bytes.size() < 100 || bytes.compare(0, 16, "SQLite format 3\0", 16) != 0)
        throw Error(422, "invalid_backup");
    BackupDirectory temporary;
    const auto file = temporary.path / "ledger.sqlite";
    {
        std::ofstream output;
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output.open(file, std::ios::binary);
        output.write(bytes.data(), bytes.size());
        output.close();
    }
    try {
        Sqlite candidate(file);
        candidate.execute("PRAGMA trusted_schema=OFF");
        if (databaseBytes(candidate) != static_cast<Json::Int64>(bytes.size()) ||
            !candidate.query("SELECT 1 FROM sqlite_schema WHERE type='view' OR sql LIKE 'CREATE VIRTUAL TABLE%' "
                             "OR (type='trigger' AND name NOT IN ('pvl_current_balance_history',"
                             "'pvl_savings_balance_history','pvl_post_expense','pvl_post_income')) LIMIT 1").empty() ||
            candidate.query("PRAGMA integrity_check(1)")[0]["integrity_check"] != "ok" || !initialized(candidate))
            throw Error(422, "invalid_backup");
        prepareDatabase(candidate, config.assets / "db/pvledger.sql");
    } catch (const std::runtime_error&) {
        throw Error(422, "invalid_backup");
    }

    // Copy typed columns into the installed schema. Keep posted balances, snapshots and
    // AUTOINCREMENT high-water marks without firing the posting rules a second time.
    Sqlite db(config.database);
    db.execute("PRAGMA trusted_schema=OFF");
    db.execute("ATTACH DATABASE ? AS incoming", {file.u8string()});
    Transaction transaction(db);
    if (setup && initialized(db)) throw Error(409, "constraint");
    db.execute("PRAGMA defer_foreign_keys=ON");
    const auto triggers = postingTriggers(db);
    for (const auto& trigger : triggers) db.execute("DROP TRIGGER " + qualified(trigger["name"].asString()));
    for (const auto& definition : tables(db)) {
        const auto name = definition["name"].asString();
        std::string names;
        for (const auto& column : db.query("PRAGMA main.table_info(" + qualified(name) + ")")) {
            if (!names.empty()) names += ",";
            names += column["name"].asString();
        }
        db.execute("DELETE FROM main." + name);
        db.execute("INSERT INTO main." + name + "(" + names + ") SELECT " + names + " FROM incoming." + name);
    }
    db.execute("DELETE FROM main.sqlite_sequence");
    db.execute("INSERT INTO main.sqlite_sequence(name,seq) SELECT name,seq FROM incoming.sqlite_sequence");
    for (const auto& trigger : triggers) db.execute(trigger["sql"].asString());
    if (!db.query("PRAGMA main.foreign_key_check").empty()) throw Error(422, "invalid_backup");
    finish(db);
    transaction.commit();
}
}
