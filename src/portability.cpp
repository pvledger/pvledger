#include "portability.hpp"
#include <algorithm>
#include <cmath>
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
        dataset.statuses = name == "income" || name == "planned-income" || name == "planned-expenses";
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
Json::Int64 positiveId(const Value& value) {
    require(value.isIntegral() && value.isInt64() && value.asInt64() > 0);
    return value.asInt64();
}
Value validateMapping(Repository& db, const Value& mapping, int role, Json::Int64 user) {
    auto account = positiveId(mapping["id_account"]);
    auto category = positiveId(mapping["id_category"]);
    auto currency = positiveId(mapping["id_currency"]);
    require(!db.query("SELECT id FROM pvl_accounts_current WHERE id=? AND (?=1 OR id_user=?)",
                      {account, role, user}).empty(), "invalid_reference");
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

Value report(Repository& db, const std::string& name, const Params& filters, int role, Json::Int64 user) {
    Params params = filters;
    bool balances = name == "balances";
    if (!balances && name != "expense-categories") throw Error(404, "not_found");
    std::string dataset = "expenses";
    if (balances) {
        const auto kind = params.extract("account_type");
        dataset = "history/" + (kind.empty() ? "current" : kind.mapped());
        require(dataset == "history/current" || dataset == "history/savings");
    }
    const auto source = datasetFor(dataset);
    const auto query = selectData(source, params, role, user);
    std::string sql = "SELECT c.id AS id_currency,COALESCE(NULLIF(c.iso,''),c.name) AS currency,";
    if (balances) sql += "e.id,e.date,e.amount,a.id AS id_account,a.name AS account";
    else sql += "category.id AS id_category,category.name AS category,SUM(e.amount) AS amount";
    sql += query.from + " JOIN pvl_currencies c ON c.id=" + source.currency;
    if (!balances) sql += " JOIN pvl_expenses_categories category ON category.id=e.id_category";
    sql += query.condition;
    if (balances) sql += " ORDER BY c.id,a.id,e.date,e.id";
    else sql += " GROUP BY c.id,category.id HAVING COUNT(e.amount)>0 ORDER BY c.id,amount DESC,category.id";
    Value result;
    result["items"] = limitedQuery(db, sql, query.args);
    return result;
}

Response exportData(Repository& db, const Value& input, bool download, int role, Json::Int64 user) {
    require(input["dataset"].isString());
    require(!input.isMember("format") || input["format"] == "csv", "csv_only");
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
    for (const auto& row : limitedQuery(db, select, query.args)) append(row, false);
    std::replace(name.begin(), name.end(), '/', '-');
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
