#include "json.h"
#include <sstream>

namespace {
struct P {
    const std::wstring& s;
    size_t i = 0;
    explicit P(const std::wstring& src) : s(src) {}
    wchar_t peek() const { return i < s.size() ? s[i] : 0; }
    wchar_t next() { return i < s.size() ? s[i++] : 0; }
    void ws() { while (peek() == L' ' || peek() == L'\t' || peek() == L'\n' || peek() == L'\r') next(); }
};
JsonValue parse_val(P& p);

std::wstring parse_raw_string(P& p) {
    std::wstring out;
    if (p.peek() != L'"') return out;
    p.next();
    while (p.peek() && p.peek() != L'"') {
        wchar_t c = p.next();
        if (c == L'\\' && p.peek()) {
            wchar_t e = p.next();
            switch (e) {
                case L'"': out += L'"'; break;
                case L'\\': out += L'\\'; break;
                case L'/': out += L'/'; break;
                case L'n': out += L'\n'; break;
                case L't': out += L'\t'; break;
                case L'r': out += L'\r'; break;
                case L'b': out += L'\b'; break;
                case L'f': out += L'\f'; break;
                case L'u': {
                    wchar_t hex[5] = {0};
                    for (int k = 0; k < 4 && p.peek(); ++k) hex[k] = p.next();
                    unsigned int cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        wchar_t h = hex[k];
                        cp <<= 4;
                        if (h >= L'0' && h <= L'9') cp |= h - L'0';
                        else if (h >= L'a' && h <= L'f') cp |= h - L'a' + 10;
                        else if (h >= L'A' && h <= L'F') cp |= h - L'A' + 10;
                    }
                    if (cp >= 0xD800 && cp <= 0xDBFF && p.peek() == L'\\') {
                        p.next();
                        if (p.peek() == L'u') {
                            p.next();
                            wchar_t hex2[5] = {0};
                            for (int k = 0; k < 4 && p.peek(); ++k) hex2[k] = p.next();
                            unsigned int lo = 0;
                            for (int k = 0; k < 4; ++k) {
                                wchar_t h = hex2[k];
                                lo <<= 4;
                                if (h >= L'0' && h <= L'9') lo |= h - L'0';
                                else if (h >= L'a' && h <= L'f') lo |= h - L'a' + 10;
                                else if (h >= L'A' && h <= L'F') lo |= h - L'A' + 10;
                            }
                            unsigned int full = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            out += (wchar_t)full;
                        }
                    } else {
                        out += (wchar_t)cp;
                    }
                    break;
                }
                default: out += e; break;
            }
        } else {
            out += c;
        }
    }
    if (p.peek() == L'"') p.next();
    return out;
}

JsonValue parse_val(P& p) {
    JsonValue v;
    p.ws();
    wchar_t c = p.peek();
    if (c == L'"') {
        v.type = JsonValue::Str;
        v.str = parse_raw_string(p);
    } else if (c == L'{') {
        v.type = JsonValue::Obj;
        p.next(); p.ws();
        if (p.peek() == L'}') { p.next(); return v; }
        while (p.peek()) {
            p.ws();
            if (p.peek() != L'"') break;
            std::wstring key = parse_raw_string(p);
            p.ws();
            if (p.peek() == L':') p.next();
            JsonValue val = parse_val(p);
            v.obj.emplace_back(key, val);
            p.ws();
            if (p.peek() == L',') { p.next(); continue; }
            break;
        }
        p.ws();
        if (p.peek() == L'}') p.next();
    } else if (c == L'[') {
        v.type = JsonValue::Arr;
        p.next(); p.ws();
        if (p.peek() == L']') { p.next(); return v; }
        while (p.peek()) {
            JsonValue item = parse_val(p);
            v.arr.push_back(item);
            p.ws();
            if (p.peek() == L',') { p.next(); continue; }
            break;
        }
        p.ws();
        if (p.peek() == L']') p.next();
    } else if (c == L't' || c == L'f') {
        v.type = JsonValue::Bool;
        if (c == L't') { p.next(); p.next(); p.next(); p.next(); v.b = true; }
        else { p.next(); p.next(); p.next(); p.next(); p.next(); v.b = false; }
    } else if (c == L'n') {
        p.next(); p.next(); p.next(); p.next();
        v.type = JsonValue::Null;
    } else {
        std::wstring num;
        while (p.peek() && (p.peek() == L'-' || p.peek() == L'+' || p.peek() == L'.' ||
               (p.peek() >= L'0' && p.peek() <= L'9') || p.peek() == L'e' || p.peek() == L'E')) {
            num += p.next();
        }
        v.type = JsonValue::Num;
        try { v.num = num.empty() ? 0 : std::stod(num); } catch (...) { v.num = 0; }
    }
    return v;
}
} // namespace

const JsonValue* JsonValue::find(const std::wstring& key) const {
    for (const auto& kv : obj) if (kv.first == key) return &kv.second;
    return nullptr;
}

JsonValue json_parse(const std::wstring& s) {
    P p(s);
    return parse_val(p);
}

std::wstring json_unescape(const std::wstring& s) {
    P p(s);
    return parse_raw_string(p);
}

std::wstring json_escape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 8);
    for (wchar_t c : s) {
        switch (c) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\t': out += L"\\t"; break;
            case L'\r': out += L"\\r"; break;
            case L'\b': out += L"\\b"; break;
            case L'\f': out += L"\\f"; break;
            default:
                if (c < 0x20) {
                    wchar_t buf[8];
                    swprintf(buf, 8, L"\\u%04x", (unsigned int)c);
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

static void stringify(const JsonValue& v, std::wstring& out, bool pretty, int depth) {
    auto ind = [&](int d) -> std::wstring { return pretty ? std::wstring(d * 2, L' ') : L""; };
    switch (v.type) {
        case JsonValue::Null: out += L"null"; break;
        case JsonValue::Bool: out += v.b ? L"true" : L"false"; break;
        case JsonValue::Num: {
            std::wstring n = std::to_wstring(v.num);
            // trim trailing zeros from to_wstring(double) like "1.500000"
            if (n.find(L'.') != std::wstring::npos && n.find(L'e') == std::wstring::npos) {
                while (n.back() == L'0') n.pop_back();
                if (n.back() == L'.') n.pop_back();
            }
            out += n;
            break;
        }
        case JsonValue::Str: out += L"\""; out += json_escape(v.str); out += L"\""; break;
        case JsonValue::Arr:
            out += L"[";
            if (v.arr.empty()) { out += L"]"; break; }
            if (pretty) out += L"\n";
            for (size_t i = 0; i < v.arr.size(); ++i) {
                out += ind(depth + 1);
                stringify(v.arr[i], out, pretty, depth + 1);
                if (i + 1 < v.arr.size()) out += L",";
                if (pretty) out += L"\n";
            }
            out += ind(depth) + L"]";
            break;
        case JsonValue::Obj:
            out += L"{";
            if (v.obj.empty()) { out += L"}"; break; }
            if (pretty) out += L"\n";
            for (size_t i = 0; i < v.obj.size(); ++i) {
                out += ind(depth + 1) + L"\"" + json_escape(v.obj[i].first) + L"\":";
                if (pretty) out += L" ";
                stringify(v.obj[i].second, out, pretty, depth + 1);
                if (i + 1 < v.obj.size()) out += L",";
                if (pretty) out += L"\n";
            }
            out += ind(depth) + L"}";
            break;
    }
}

std::wstring json_stringify(const JsonValue& v, bool pretty) {
    std::wstring out;
    stringify(v, out, pretty, 0);
    return out;
}
