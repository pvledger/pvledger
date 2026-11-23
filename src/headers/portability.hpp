#pragma once
#include "pvledger.hpp"

namespace pvl {
struct Resource {
    const char* route;
    const char* table;
    const char* label;
    enum Access { Read, Write, Admin } access;
    const char* owner = "";
};
extern const std::vector<Resource> resources;

Value lookup(Repository& db, const std::string& route, int role, Json::Int64 user, int page);
Value report(Repository& db, const std::string& name, const Params& filters, int role, Json::Int64 user);
Response exportData(Repository& db, const Value& input, bool download, int role, Json::Int64 user);
Value previewExpenses(Repository& db, const Value& input, int role, Json::Int64 user);
Value commitExpenses(Repository& db, const Value& preview, const Value& selection, int role, Json::Int64 user);
}
