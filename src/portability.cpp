#include "portability.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <set>

namespace pvl {
namespace {
constexpr unsigned batchLimit = 500;
constexpr int resultLimit = 5000;

void require(bool condition, const char* key = "invalid_input") {
    if (!condition) throw Error(422, key);
}
const Resource& resourceFor(const std::string& route) {
    for (const auto& resource : resources)
        if (route == resource.route) return resource;
    throw Error(404, "not_found");
}
std::string labelFor(const std::string& table, const std::string& alias) {
    const auto col = alias + ".";
    if (table == "pvl_users")
        return "COALESCE(NULLIF(trim(" + col + "displayname),''),NULLIF(trim(" + col +
               "username),''),'User #'||" + col + "id)";
    if (table == "pvl_countries" || table == "pvl_currencies")
        return "COALESCE(NULLIF(" + col + "name,'')," + col + "iso,'#'||" + col +
               "id)||CASE WHEN " + col + "iso IS NULL OR " + col + "name IS NULL THEN '' ELSE ' ('||" +
               col + "iso||')' END";
    return col + "name";
}
struct Dataset {
    std::string table, parent, account, currency;
    bool dates = false, statuses = false;
};

Dataset datasetFor(const std::string& name) {
    if (name == "history/current" || name == "history/savings") {
        const auto kind = name.substr(8);
        return {"pvl_account_history_" + kind, "pvl_accounts_" + kind, "e.id_account", "a.id_currency", true};
    }
    const auto& resource = resourceFor(name);
    if (name == "users" || name == "roles") throw Error(403, "forbidden");
    Dataset dataset{resource.table, "", "", ""};
    const std::string owner = resource.owner;
    if (owner == "id_user") {
        dataset.account = "e.id";
        dataset.currency = "e.id_currency";
    } else if (owner == "id_account") {
        dataset.parent = "pvl_accounts_current";
        dataset.account = "e.id_account";
        dataset.dates = true;
        dataset.currency = name == "expenses" || name == "planned-expenses" ? "e.id_currency" : "a.id_currency";
        dataset.statuses = name == "planned-income" || name == "planned-expenses";
    }
    return dataset;
}
struct Selection {
    std::string from, condition;
    std::vector<Value> args;
    void filter(const std::string& expression, const Value& value) {
        condition += (condition.empty() ? " WHERE " : " AND ") + expression;
        args.push_back(value);
    }
};
Selection selectData(const Dataset& dataset, const Params& params, int role, Json::Int64 user) {
    Selection query{" FROM " + dataset.table + " e", "", {}};
    if (!dataset.parent.empty()) query.from += " JOIN " + dataset.parent + " a ON a.id=e.id_account";
    if (role != 1 && !dataset.account.empty())
        query.filter(dataset.parent.empty() ? "e.id_user=?" : "a.id_user=?", user);
    for (const auto& [key, text] : params) {
        const Json::Int64 value = integer(text);
        if (key == "from" || key == "to") {
            require(dataset.dates && value >= -62135596800LL && value <= 253402300799LL);
            query.filter(key == "from" ? "e.date>=?" : "e.date<=?", value);
        } else if (key == "status" && dataset.statuses) query.filter("e.status=?", value);
        else if (key == "id_account" && !dataset.account.empty() && value > 0) query.filter(dataset.account + "=?", value);
        else if (key == "id_currency" && !dataset.currency.empty() && value > 0) query.filter(dataset.currency + "=?", value);
        else throw Error(422, "invalid_input");
    }
    if (params.count("from") && params.count("to")) require(integer(params.at("from")) <= integer(params.at("to")));
    return query;
}
Value limitedQuery(Repository& db, const std::string& sql, const std::vector<Value>& args) {
    auto rows = db.query(sql + " LIMIT " + std::to_string(resultLimit + 1), args);
    require(rows.size() <= resultLimit, "narrow_filters");
    return rows;
}
std::string csvCell(const Value& value) {
    if (value.isNull()) return "";
    if (!value.isString()) return json(value);
    std::string text = value.asString();
    const auto first = text.find_first_not_of(" \t\r\n");
    if ((!text.empty() && (text[0] == '\t' || text[0] == '\r' || text[0] == '\n')) ||
        (first != std::string::npos && std::string("=+-@").find(text[first]) != std::string::npos))
        text.insert(0, "'");
    std::string out = "\"";
    for (char c : text) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + '"';
}

std::string exportDate(const Value& value, const char* format) {
    const auto seconds = static_cast<std::time_t>(value.asInt64());
    std::tm utc{};
    char date[32];
#ifdef _WIN32
    // The Windows CRT only accepts a subset of the supported years. Shift by
    // Gregorian 400-year cycles and expand the year before calling strftime.
    require(seconds >= -62135596800LL && seconds <= 253402300799LL);
    constexpr std::time_t cycle = 146097LL * 86400;
    constexpr std::time_t year2000 = 946684800;
    const auto offset = seconds - year2000;
    const auto cycles = offset / cycle - (offset % cycle < 0 ? 1 : 0);
    const auto normalized = seconds - cycles * cycle;
    require(gmtime_s(&utc, &normalized) == 0);
    char year[5];
    std::snprintf(year, sizeof(year), "%04d", utc.tm_year + 1900 + static_cast<int>(cycles) * 400);
    std::string expanded(format);
    expanded.replace(expanded.find("%04Y"), 4, year);
    require(std::strftime(date, sizeof(date), expanded.c_str(), &utc));
#else
    require(gmtime_r(&seconds, &utc) && std::strftime(date, sizeof(date), format, &utc));
#endif
    return date;
}

std::string exportAmount(double value) {
    char buffer[400];
    const auto output = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed);
    require(output.ec == std::errc{});
    return {buffer, output.ptr};
}

std::string exportText(const Value& value, std::size_t limit = std::string::npos) {
    std::string out;
    for (const unsigned char ch : value.asString()) {
        if ((ch & 0xc0) != 0x80) {
            if (!limit) break;
            --limit;
        }
        out += ch < 32 ? ' ' : static_cast<char>(ch);
    }
    return out;
}

Response transactionExport(Repository& db, const std::string& name, const Dataset& source,
                           const Selection& query, const std::string& format, bool download) {
    const auto clauses = query.from + " JOIN pvl_currencies c ON c.id=" + source.currency + query.condition;
    const auto summary = db.query("SELECT COUNT(*) AS n,COUNT(DISTINCT a.id) AS accounts,"
        "COUNT(DISTINCT c.id) AS currencies,MIN(c.iso) AS iso,MIN(e.date) AS first,MAX(e.date) AS last,"
        "MIN(a.id) AS account,MIN(a.amount) AS balance,MIN(a.id_currency) AS account_currency,"
        "MIN(c.id) AS currency,COUNT(CASE WHEN typeof(e.date)='integer' AND e.date BETWEEN -62135596800 AND 253402300799"
        " AND abs(e.amount)<=1.7976931348623157e308 THEN 1 END) AS valid" + clauses, query.args)[0];
    const auto count = summary["n"].asInt64();
    require(count > 0, "export_no_transactions");
    require(summary["accounts"].asInt() == 1 && summary["currencies"].asInt() == 1, "export_single_scope");
    require(summary["valid"].asInt64() == count, "export_incomplete_transactions");
    const bool qif = format == "qif";
    const auto iso = summary["iso"].asString();
    require(qif || (iso.size() == 3 && iso.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::string::npos),
            "currency_missing");
    const auto select = "SELECT e.id,e.date,e.name,e.description,e.amount,c.iso AS currency" +
        clauses + " ORDER BY e.date,e.id";
    if (!download) {
        Value output;
        for (const auto key : {"date", "name", "description", "amount", "currency"}) output["columns"].append(key);
        output["total"] = count;
        output["limit"] = resultLimit;
        output["items"] = db.query(select + " LIMIT 10", query.args);
        if (name == "expenses")
            for (auto& row : output["items"]) row["amount"] = -row["amount"].asDouble();
        return {200, "application/json; charset=utf-8", json(output)};
    }
    require(count <= resultLimit, "narrow_filters");
    std::string body = "!Type:Bank\r\n";
    constexpr const char* ofxDate = "%04Y%m%d%H%M%S";
    const Value now = Json::Int64(std::time(nullptr));
    constexpr const char* ok = "<STATUS><CODE>0</CODE><SEVERITY>INFO</SEVERITY></STATUS>";
    if (!qif) {
        body = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
               "<?OFX OFXHEADER=\"200\" VERSION=\"220\" SECURITY=\"NONE\" OLDFILEUID=\"NONE\" NEWFILEUID=\"NONE\"?>\r\n"
               "<OFX><SIGNONMSGSRSV1><SONRS>";
        body += std::string(ok) + "<DTSERVER>" + exportDate(now, ofxDate) + "</DTSERVER>"
                "<LANGUAGE>ENG</LANGUAGE></SONRS></SIGNONMSGSRSV1>\r\n"
                "<BANKMSGSRSV1><STMTTRNRS><TRNUID>0</TRNUID>" + ok + "<STMTRS><CURDEF>" + iso +
                "</CURDEF><BANKACCTFROM><BANKID>PVLedger</BANKID><ACCTID>P" + summary["account"].asString() +
                "</ACCTID><ACCTTYPE>CHECKING</ACCTTYPE></BANKACCTFROM><BANKTRANLIST><DTSTART>" +
                exportDate(summary["first"], ofxDate) + "</DTSTART><DTEND>" +
                exportDate(summary["last"], ofxDate) + "</DTEND>\r\n";
    }
    for (const auto& row : limitedQuery(db, select, query.args)) {
        const double amount = row["amount"].asDouble() * (name == "expenses" ? -1 : 1);
        const auto payee = exportText(row["name"], qif ? std::string::npos : 32);
        const auto memo = exportText(row["description"], qif ? std::string::npos : 255);
        if (qif) {
            body += "D" + exportDate(row["date"], "%m/%d/%04Y") + "\r\nT" + exportAmount(amount) +
                    "\r\nP" + payee + "\r\n";
            if (!memo.empty()) body += "M" + memo + "\r\n";
            body += "^\r\n";
        } else {
            body += std::string("<STMTTRN><TRNTYPE>") + (amount < 0 ? "DEBIT" : "CREDIT") +
                    "</TRNTYPE><DTPOSTED>" + exportDate(row["date"], ofxDate) + "</DTPOSTED><TRNAMT>" +
                    exportAmount(amount) + "</TRNAMT><FITID>pvledger-" + name + "-" +
                    row["id"].asString() + "</FITID><NAME>" + escape(payee) + "</NAME>";
            const auto extendedName = exportText(row["name"], 100);
            if (extendedName != payee) body += "<EXTDNAME>" + escape(extendedName) + "</EXTDNAME>";
            if (!memo.empty()) body += "<MEMO>" + escape(memo) + "</MEMO>";
            body += "</STMTTRN>\r\n";
        }
        require(body.size() <= 8 * 1024 * 1024, "narrow_filters");
    }
    if (!qif) {
        body += "</BANKTRANLIST>";
        if (summary["currency"] == summary["account_currency"] && summary["balance"].isNumeric() &&
            std::isfinite(summary["balance"].asDouble()))
            body += "<LEDGERBAL><BALAMT>" + exportAmount(summary["balance"].asDouble()) +
                    "</BALAMT><DTASOF>" + exportDate(now, ofxDate) + "</DTASOF></LEDGERBAL>";
        body += "</STMTRS></STMTTRNRS></BANKMSGSRSV1></OFX>\r\n";
    }
    const auto type = qif ? "application/qif" : format == "qfx" ? "application/vnd.intu.qfx" : "application/x-ofx";
    Response result(200, type, std::move(body));
    result.filename = "pvledger-" + name + "." + format;
    return result;
}
Json::Int64 positiveId(const Value& value) {
    require(value.isIntegral() && value.isInt64() && value.asInt64() > 0);
    return value.asInt64();
}
Value validateMapping(Repository& db, const Value& mapping, int role, Json::Int64 user) {
    auto account = positiveId(mapping["id_account"]);
    auto category = positiveId(mapping["id_category"]);
    auto currency = positiveId(mapping["id_currency"]);
    const auto accounts = db.query("SELECT id_currency,amount FROM pvl_accounts_current WHERE id=? AND (?=1 OR id_user=?)",
                                   {account, role, user});
    require(!accounts.empty(), "invalid_reference");
    require(accounts[0]["id_currency"].asInt64() == currency, "posting_currency_mismatch");
    require(!accounts[0]["amount"].isNull(), "opening_balance_required");
    const auto currencies = db.query("SELECT iso FROM pvl_currencies WHERE id=?", {currency});
    require(!currencies.empty() && !db.query("SELECT id FROM pvl_expenses_categories WHERE id=?", {category}).empty(),
            "invalid_reference");
    return currencies[0]["iso"];
}
bool duplicateExpense(Repository& db, const Value& mapping, const Value& row) {
    return !db.query("SELECT id FROM pvl_expenses WHERE id_account=? AND id_currency=? AND date=? AND amount=? AND name=? LIMIT 1",
                     {mapping["id_account"], mapping["id_currency"], row["date"], row["amount"], row["name"]}).empty();
}
bool validText(const Value& value, unsigned limit, bool required) {
    if (value.isNull()) return !required;
    if (!value.isString()) return false;
    const auto text = value.asString();
    return text.size() <= limit && text.find('\0') == std::string::npos &&
           (!required || text.find_first_not_of(" \n\r\t") != std::string::npos);
}
}

Value lookup(Repository& db, const std::string& route, int role, Json::Int64 user, int page) {
    const auto& target = resourceFor(route);
    static const std::set<std::string> allowed{"accounts/current", "accounts/savings", "accounts/credit", "banks",
                                        "countries", "currencies", "expense-categories", "users", "roles"};
    if (!allowed.count(route)) throw Error(404, "not_found");
    if (route == "roles" && role != 1) throw Error(403, "forbidden");
    Selection query{" FROM " + std::string(target.table) + " e", "", {}};
    if (role != 1 && route == "users") query.filter("e.id=?", user);
    if (role != 1 && std::string(target.owner) == "id_user") query.filter("e.id_user=?", user);
    query.args.emplace_back((page - 1) * 100);
    return db.query("SELECT e.id," + labelFor(target.table, "e") + " AS label" + query.from + query.condition +
                    " ORDER BY label COLLATE NOCASE,e.id LIMIT 100 OFFSET ?", query.args);
}

static Value estimates(Repository& db, const Params& filters, int role, Json::Int64 user) {
    std::string entries;
    std::vector<Value> args;
    for (const std::string name : {"income", "expenses", "planned-income", "planned-expenses"}) {
        const bool planned = name.rfind("planned-", 0) == 0;
        const bool income = name == "income" || name == "planned-income";
        auto source = datasetFor(name);
        source.statuses = false;
        auto query = selectData(source, filters, role, user);
        if (!entries.empty()) entries += " UNION ALL ";
        entries += "SELECT " + source.currency + " AS id_currency,e.date," +
                   (planned ? "'planned'" : "'actual'") + " AS basis," +
                   (income ? "e.amount AS income,0 AS expenses" : "0 AS income,e.amount AS expenses") +
                   query.from + query.condition + (query.condition.empty() ? " WHERE " : " AND ") +
                   "e.amount IS NOT NULL AND e.date" + (planned ? ">=" : "<") +
                   "CAST(strftime('%s','now','start of day','+1 day') AS INTEGER)";
        if (planned) entries += " AND (e.status=0 OR e.status IS NULL)";
        args.insert(args.end(), query.args.begin(), query.args.end());
    }
    Value result;
    result["items"] = limitedQuery(db,
        "WITH entries AS (" + entries + ") SELECT c.id AS id_currency,COALESCE(NULLIF(c.iso,''),c.name) AS currency,"
        "CAST(strftime('%s',e.date,'unixepoch','start of day') AS INTEGER) AS day,e.basis,"
        "SUM(e.income) AS income,SUM(e.expenses) AS expenses,SUM(e.income)-SUM(e.expenses) AS net"
        " FROM entries e JOIN pvl_currencies c ON c.id=e.id_currency GROUP BY c.id,day,e.basis"
        " HAVING day IS NOT NULL ORDER BY c.id,day", args);
    return result;
}

static Value balancesReport(Repository& db, const Params& filters, int role, Json::Int64 user, bool projection) {
    Params scope = filters;
    const auto through = scope.extract("to");
    require(through.empty() || projection);
    require(scope.empty() || (scope.size() == 1 && scope.count("id_currency")));
    std::string accounts;
    std::vector<Value> args;
    for (const std::string kind : {"current", "savings"}) {
        const auto query = selectData(datasetFor("accounts/" + kind), scope, role, user);
        if (!accounts.empty()) accounts += " UNION ALL ";
        accounts += "SELECT '" + kind + "' AS account_type,e.id AS id_account,e.name AS account,e.id_currency,e.amount" +
                    query.from + query.condition + (query.condition.empty() ? " WHERE " : " AND ") +
                    "typeof(e.amount) IN ('integer','real') AND abs(e.amount)<=1.7976931348623157e308";
        args.insert(args.end(), query.args.begin(), query.args.end());
    }
    std::string sql = "WITH accounts AS (" + accounts + ")";
    if (projection) {
        sql += R"sql(,
            today(day) AS (SELECT CAST(strftime('%s','now','start of day') AS INTEGER)),
            plans AS (
                SELECT a.id_account,a.id_currency,e.date,e.amount AS delta
                FROM accounts a JOIN pvl_income_planned e ON e.id_account=a.id_account
                WHERE a.account_type='current' AND e.status=0
                UNION ALL
                SELECT a.id_account,a.id_currency,e.date,-e.amount
                FROM accounts a JOIN pvl_expenses_planned e ON e.id_account=a.id_account
                WHERE a.account_type='current' AND a.id_currency=e.id_currency AND (e.status=0 OR e.status IS NULL)
                      AND typeof(e.amount) IN ('integer','real')
            ),
            changes AS (
                SELECT account_type,id_account,id_currency,day,amount AS movement,0 AS delta FROM accounts CROSS JOIN today
                UNION ALL
                SELECT 'current',id_account,id_currency,
                       CAST(strftime('%s',date,'unixepoch','start of day') AS INTEGER),
                       SUM(delta),SUM(delta)
                FROM plans CROSS JOIN today
                WHERE typeof(date)='integer' AND date BETWEEN today.day+86400 AND ?
                      AND typeof(delta) IN ('integer','real') AND abs(delta)<=1.7976931348623157e308
                GROUP BY id_account,id_currency,date/86400
            ),
            series AS (
                SELECT * FROM changes
                UNION ALL
                SELECT NULL,NULL,id_currency,day,SUM(movement),SUM(delta) FROM changes GROUP BY id_currency,day
            )
            SELECT s.account_type,s.id_account,a.account,s.id_currency,COALESCE(NULLIF(c.iso,''),c.name) AS currency,
                   s.day,s.delta,SUM(s.movement) OVER (
                       PARTITION BY s.id_currency,s.account_type,s.id_account ORDER BY s.day
                   ) AS amount
            FROM series s JOIN pvl_currencies c ON c.id=s.id_currency
            LEFT JOIN accounts a ON a.account_type=s.account_type AND a.id_account=s.id_account
            ORDER BY s.id_currency,s.account_type,s.id_account,s.day)sql";
        const auto end = through.empty() ? 253402300799LL : integer(through.mapped());
        require(end >= -62135596800LL && end <= 253402300799LL);
        args.emplace_back(Json::Int64(end));
    } else {
        sql += " SELECT a.*,COALESCE(NULLIF(c.iso,''),c.name) AS currency"
               " FROM accounts a JOIN pvl_currencies c ON c.id=a.id_currency"
               " ORDER BY c.id,a.amount DESC,a.account_type,a.id_account";
    }
    Value result;
    result["items"] = limitedQuery(db, sql, args);
    return result;
}

Value report(Repository& db, const std::string& name, const Params& filters, int role, Json::Int64 user) {
    if (name == "estimates") return estimates(db, filters, role, user);
    if (name == "balance-projection" || name == "fund-distribution")
        return balancesReport(db, filters, role, user, name == "balance-projection");
    Params params = filters;
    const bool balances = name == "balances";
    const bool spending = name == "spending";
    if (!balances && !spending && name != "expense-categories") throw Error(404, "not_found");
    std::string dataset = "expenses";
    if (balances) {
        const auto kind = params.extract("account_type");
        dataset = "history/" + (kind.empty() ? "current" : kind.mapped());
        require(dataset == "history/current" || dataset == "history/savings");
    }
    const auto source = datasetFor(dataset);
    const auto query = selectData(source, params, role, user);
    std::string sql = "SELECT c.id AS id_currency,COALESCE(NULLIF(c.iso,''),c.name) AS currency,";
    const auto from = query.from + " JOIN pvl_currencies c ON c.id=" + source.currency;
    if (balances) {
        sql += "e.id,e.date,e.amount,a.id AS id_account,a.name AS account" + from + query.condition +
               " ORDER BY c.id,a.id,e.date,e.id";
    } else if (spending) {
        sql += "CAST(strftime('%s',e.date,'unixepoch','start of day') AS INTEGER) AS day,SUM(e.amount) AS amount" +
               from + query.condition + " GROUP BY c.id,day HAVING day IS NOT NULL AND COUNT(e.amount)>0 ORDER BY c.id,day";
    } else {
        sql += "category.id AS id_category,category.name AS category,SUM(e.amount) AS amount" + from +
               " JOIN pvl_expenses_categories category ON category.id=e.id_category" + query.condition +
               " GROUP BY c.id,category.id HAVING COUNT(e.amount)>0 ORDER BY c.id,amount DESC,category.id";
    }
    Value result;
    result["items"] = limitedQuery(db, sql, query.args);
    return result;
}

Response exportData(Repository& db, const Value& input, bool download, int role, Json::Int64 user) {
    require(input["dataset"].isString());
    require(!input.isMember("format") || input["format"].isString());
    const auto format = input.get("format", "csv").asString();
    const bool workbook = format == "ods" || format == "xlsx";
    require(workbook || format == "csv" || format == "qif" || format == "ofx" || format == "qfx", "export_format");
    auto name = input["dataset"].asString();
    const auto source = datasetFor(name);
    Params params;
    require(!input.isMember("filters") || input["filters"].isObject());
    for (const auto& key : input["filters"].getMemberNames()) {
        const auto& value = input["filters"][key];
        require(value.isIntegral() || value.isString());
        params[key] = value.asString();
    }
    auto query = selectData(source, params, role, user);
    if (!workbook && format != "csv") {
        require(name == "expenses" || name == "income", "export_transaction_dataset");
        require(!input.isMember("columns"), "export_fixed_columns");
        return transactionExport(db, name, source, query, format, download);
    }
    std::map<std::string, std::string> expressions;
    Value columns(Json::arrayValue);
    auto add = [&](const std::string& key, const std::string& expression) {
        columns.append(key);
        expressions[key] = expression;
    };
    for (const auto& field : db.query("PRAGMA table_info(" + source.table + ")")) {
        const auto key = field["name"].asString();
        add(key, "e." + key);
    }
    for (const auto& fk : db.query("PRAGMA foreign_key_list(" + source.table + ")")) {
        const auto key = fk["from"].asString(), table = fk["table"].asString(), alias = "ref_" + key;
        query.from += " LEFT JOIN " + table + " " + alias + " ON " + alias + ".id=e." + key;
        if (role != 1 && table == "pvl_accounts_current") {
            query.from += " AND " + alias + ".id_user=" + std::to_string(user);
        }
        add(key + "_label", labelFor(table, alias));
    }
    if (!source.currency.empty() && !expressions.count("id_currency")) {
        query.from += " JOIN pvl_currencies currency ON currency.id=" + source.currency;
        add("id_currency", "currency.id");
        add("id_currency_label", labelFor("pvl_currencies", "currency"));
    }
    const Value& selected = input.isMember("columns") ? input["columns"] : columns;
    require(selected.isArray() && !selected.empty() && selected.size() <= columns.size());
    std::set<std::string> seen;
    std::string sql;
    for (const auto& field : selected) {
        require(field.isString());
        const auto key = field.asString();
        require(expressions.count(key) && seen.insert(key).second);
        if (!sql.empty()) sql += ",";
        sql += expressions.at(key) + " AS " + key;
    }
    const auto clauses = query.from + query.condition;
    const auto count = db.query("SELECT COUNT(*) AS n" + clauses, query.args)[0]["n"];
    const auto select = "SELECT " + sql + clauses + " ORDER BY e.id";
    if (!download) {
        Value output;
        output["columns"] = columns;
        output["total"] = count;
        output["limit"] = resultLimit;
        output["items"] = db.query(select + " LIMIT 10", query.args);
        return {200, "application/json; charset=utf-8", json(output)};
    }
    require(count.asInt64() <= resultLimit, "narrow_filters");
    std::replace(name.begin(), name.end(), '/', '-');
    auto rows = limitedQuery(db, select, query.args);
    if (workbook) return spreadsheet(format, name, selected, rows);
    std::string csv;
    const auto append = [&](const Value& row, bool header) {
        bool first = true;
        for (const auto& field : selected) {
            if (!first) csv += ',';
            csv += csvCell(header ? field : row[field.asString()]);
            first = false;
        }
        csv += "\r\n";
        require(csv.size() <= 8 * 1024 * 1024, "narrow_filters");
    };
    append({}, true);
    for (const auto& row : rows) append(row, false);
    Response result(200, "text/csv; charset=utf-8", std::move(csv));
    result.filename = "pvledger-" + name + ".csv";
    return result;
}

Value previewExpenses(Repository& db, const Value& input, int role, Json::Int64 user) {
    require(input["dataset"] == "expenses", "expense_target");
    const auto iso = validateMapping(db, input, role, user);
    const auto& rows = input["rows"];
    require(rows.isArray() && !rows.empty() && rows.size() <= batchLimit, "import_limit");
    Value output;
    for (auto key : {"id_account", "id_currency", "id_category"}) output[key] = input[key];
    output["rows"] = Value(Json::arrayValue);
    std::set<std::string> signatures, identifiers;
    for (const auto& source : rows) {
        require(source.isObject());
        Value row;
        for (auto key : {"name", "description", "date", "amount", "source_id", "currency", "error"}) row[key] = source[key];
        if (!validText(row["error"], 256, false)) row["error"] = "invalid_input";
        if (row["error"].asString().empty()) {
            if (!validText(row["name"], 4096, true) || !validText(row["description"], 4096, false) ||
                !validText(row["source_id"], 256, false) || !row["date"].isInt64() || !row["date"].isIntegral() ||
                row["date"].asInt64() < -62135596800LL || row["date"].asInt64() > 253402300799LL ||
                !row["amount"].isNumeric() || !std::isfinite(row["amount"].asDouble()) || row["amount"].asDouble() <= 0)
                row["error"] = "invalid_expense";
            else if (!row["currency"].isNull() && row["currency"] != iso) row["error"] = "currency_mismatch";
        }
        if (row["error"].asString().empty()) {
            Value signature(Json::arrayValue);
            signature.append(row["name"]);
            signature.append(row["date"]);
            signature.append(row["amount"].asDouble());
            bool repeated = !signatures.insert(json(signature)).second;
            const auto ref = row["source_id"].asString();
            if (!ref.empty() && !identifiers.insert(ref).second) repeated = true;
            row["duplicate"] = repeated || duplicateExpense(db, output, row);
        }
        output["rows"].append(std::move(row));
    }
    return output;
}

Value commitExpenses(Repository& db, const Value& preview, const Value& selection, int role, Json::Int64 user) {
    require(selection.isArray() && !selection.empty() && selection.size() <= preview["rows"].size());
    Transaction transaction(db);
    const auto iso = validateMapping(db, preview, role, user);
    std::set<unsigned> indices;
    for (const auto& index : selection) {
        require(index.isUInt() && index.isIntegral() && index.asUInt() < preview["rows"].size() && indices.insert(index.asUInt()).second);
        const auto& row = preview["rows"][index.asUInt()];
        require(row["error"].asString().empty(), "invalid_expense");
        if (!row["currency"].isNull() && row["currency"] != iso) throw Error(409, "preview_needed");
        if (!row["duplicate"].asBool() && duplicateExpense(db, preview, row)) throw Error(409, "preview_needed");
    }
    // Check every selected row against the same database state before inserting any of them.
    for (const auto index : indices) {
        const auto& row = preview["rows"][index];
        db.execute("INSERT INTO pvl_expenses(id_account,id_currency,id_category,name,date,amount,description) "
                   "VALUES(?,?,?,?,?,?,?)", {preview["id_account"], preview["id_currency"], preview["id_category"],
                   row["name"], row["date"], row["amount"], row["description"]});
    }
    transaction.commit();
    Value result;
    result["imported"] = selection.size();
    result["skipped"] = preview["rows"].size() - selection.size();
    result["failed"] = 0;
    return result;
}
}
