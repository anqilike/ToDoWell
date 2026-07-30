#pragma once
#include <string>
#include <vector>
#include <map>

// Minimal JSON value used only for this app's data files.
struct JsonValue {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double num = 0;
    std::wstring str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::wstring, JsonValue>> obj;
    const JsonValue* find(const std::wstring& key) const;
};
JsonValue json_parse(const std::wstring& s);
std::wstring json_stringify(const JsonValue& v, bool pretty = true);
// Escape/unescape a JSON string body (no surrounding quotes).
std::wstring json_escape(const std::wstring& s);
std::wstring json_unescape(const std::wstring& s);
