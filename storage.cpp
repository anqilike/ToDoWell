#include "storage.h"
#include "json.h"
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <sstream>
#include <cwchar>

std::wstring utf8_to_w(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], n);
    return ws;
}
std::string w_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring exe_dir() {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p = path;
    size_t bs = p.find_last_of(L'\\');
    return bs != std::wstring::npos ? p.substr(0, bs + 1) : p;
}

static bool read_file(const std::wstring& path, std::string& out) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f.is_open()) return false;
    std::stringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
}
static bool write_file(const std::wstring& path, const std::string& data) {
    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f.write(data.data(), (std::streamsize)data.size());
    return true;
}

std::vector<Project> load_projects() {
    std::string raw;
    if (!read_file(exe_dir() + L"todos.json", raw)) return {};
    JsonValue root = json_parse(utf8_to_w(raw));
    std::vector<Project> projs;
    if (root.type != JsonValue::Arr) return projs;
    for (const auto& it : root.arr) {
        if (it.type != JsonValue::Obj) continue;
        Project p;
        const JsonValue* name = it.find(L"name");
        const JsonValue* todos = it.find(L"todos");
        // Legacy format: array of plain todo objects without a name -> wrap once.
        if (!name && it.find(L"text")) {
            p.name = L"\u9ed8\u8ba4\u9879\u76ee";
            TodoItem t;
            const JsonValue* tx = it.find(L"text");
            const JsonValue* dn = it.find(L"done");
            if (tx && tx->type == JsonValue::Str) t.text = tx->str;
            if (dn && dn->type == JsonValue::Bool) t.done = dn->b;
            if (!t.text.empty()) p.todos.push_back(t);
            projs.push_back(p);
            continue;
        }
        if (name && name->type == JsonValue::Str) p.name = name->str;
        if (todos && todos->type == JsonValue::Arr) {
            for (const auto& tt : todos->arr) {
                TodoItem t;
                const JsonValue* tx = tt.find(L"text");
                const JsonValue* dn = tt.find(L"done");
                const JsonValue* cr = tt.find(L"created");
                if (tx && tx->type == JsonValue::Str) t.text = tx->str;
                if (dn && dn->type == JsonValue::Bool) t.done = dn->b;
                if (cr && cr->type == JsonValue::Str) t.created = cr->str;
                if (!t.text.empty()) p.todos.push_back(t);
            }
        }
        projs.push_back(p);
    }
    return projs;
}

void save_projects(const std::vector<Project>& projs) {
    JsonValue root; root.type = JsonValue::Arr;
    for (const auto& proj : projs) {
        JsonValue po; po.type = JsonValue::Obj;
        JsonValue nm; nm.type = JsonValue::Str; nm.str = proj.name;
        JsonValue ts; ts.type = JsonValue::Arr;
        for (const auto& t : proj.todos) {
            JsonValue to; to.type = JsonValue::Obj;
            JsonValue tx; tx.type = JsonValue::Str; tx.str = t.text;
            JsonValue dn; dn.type = JsonValue::Bool; dn.b = t.done;
            JsonValue cr; cr.type = JsonValue::Str; cr.str = t.created;
            to.obj.emplace_back(L"text", tx);
            to.obj.emplace_back(L"done", dn);
            to.obj.emplace_back(L"created", cr);
            ts.arr.push_back(to);
        }
        po.obj.emplace_back(L"name", nm);
        po.obj.emplace_back(L"todos", ts);
        root.arr.push_back(po);
    }
    write_file(exe_dir() + L"todos.json", w_to_utf8(json_stringify(root, true)));
}

Config load_config() {
    Config cfg;
    std::string raw;
    if (!read_file(exe_dir() + L"config.json", raw)) return cfg;
    JsonValue root = json_parse(utf8_to_w(raw));
    if (root.type != JsonValue::Obj) return cfg;
    const JsonValue* tp = root.find(L"title_prefix");
    if (tp && tp->type == JsonValue::Str) cfg.title_prefix = tp->str;
    const JsonValue* as = root.find(L"auto_start");
    if (as && as->type == JsonValue::Bool) cfg.auto_start = as->b;
    const JsonValue* sa = root.find(L"snap_anim");
    if (sa && sa->type == JsonValue::Num) cfg.snap_anim = (int)sa->num;
    const JsonValue* dp = root.find(L"default_pos");
    if (dp && dp->type == JsonValue::Num) cfg.default_pos = (int)dp->num;
    return cfg;
}

void save_config(const Config& cfg) {
    JsonValue root; root.type = JsonValue::Obj;
    JsonValue tp; tp.type = JsonValue::Str; tp.str = cfg.title_prefix;
    JsonValue as; as.type = JsonValue::Bool; as.b = cfg.auto_start;
    JsonValue sa; sa.type = JsonValue::Num; sa.num = (double)cfg.snap_anim;
    JsonValue dp; dp.type = JsonValue::Num; dp.num = (double)cfg.default_pos;
    root.obj.emplace_back(L"title_prefix", tp);
    root.obj.emplace_back(L"auto_start", as);
    root.obj.emplace_back(L"snap_anim", sa);
    root.obj.emplace_back(L"default_pos", dp);
    write_file(exe_dir() + L"config.json", w_to_utf8(json_stringify(root, false)));
}

std::vector<HistoryItem> load_history() {
    std::string raw;
    if (!read_file(exe_dir() + L"history.json", raw)) return {};
    JsonValue root = json_parse(utf8_to_w(raw));
    std::vector<HistoryItem> items;
    if (root.type != JsonValue::Arr) return items;
    for (const auto& it : root.arr) {
        if (it.type != JsonValue::Obj) continue;
        HistoryItem h;
        const JsonValue* pj = it.find(L"project");
        const JsonValue* tx = it.find(L"text");
        const JsonValue* cr = it.find(L"created");
        const JsonValue* cp = it.find(L"completed");
        if (pj && pj->type == JsonValue::Str) h.project = pj->str;
        if (tx && tx->type == JsonValue::Str) h.text = tx->str;
        if (cr && cr->type == JsonValue::Str) h.created = cr->str;
        if (cp && cp->type == JsonValue::Str) h.completed = cp->str;
        if (!h.text.empty()) items.push_back(h);
    }
    return items;
}

void save_history(const std::vector<HistoryItem>& items) {
    JsonValue root; root.type = JsonValue::Arr;
    for (const auto& h : items) {
        JsonValue o; o.type = JsonValue::Obj;
        JsonValue pj; pj.type = JsonValue::Str; pj.str = h.project;
        JsonValue tx; tx.type = JsonValue::Str; tx.str = h.text;
        JsonValue cr; cr.type = JsonValue::Str; cr.str = h.created;
        JsonValue cp; cp.type = JsonValue::Str; cp.str = h.completed;
        o.obj.emplace_back(L"project", pj);
        o.obj.emplace_back(L"text", tx);
        o.obj.emplace_back(L"created", cr);
        o.obj.emplace_back(L"completed", cp);
        root.arr.push_back(o);
    }
    write_file(exe_dir() + L"history.json", w_to_utf8(json_stringify(root, true)));
}

std::wstring now_iso() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%04d-%02d-%02dT%02d:%02d:%02d",
               (int)st.wYear, (int)st.wMonth, (int)st.wDay,
               (int)st.wHour, (int)st.wMinute, (int)st.wSecond);
    return buf;
}

static bool parse_iso(const std::wstring& s, SYSTEMTIME& st) {
    st = {};
    if (s.size() < 19) return false;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (swscanf_s(s.c_str(), L"%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return false;
    st.wYear = (WORD)y; st.wMonth = (WORD)mo; st.wDay = (WORD)d;
    st.wHour = (WORD)h; st.wMinute = (WORD)mi; st.wSecond = (WORD)se;
    return true;
}

std::wstring iso_to_display(const std::wstring& iso) {
    if (iso.empty()) return L"\u2014"; // —
    if (iso.size() >= 16) return iso.substr(0, 10) + L" " + iso.substr(11, 5);
    return iso;
}

std::wstring duration_text(const std::wstring& created, const std::wstring& completed) {
    SYSTEMTIME a, b;
    if (!parse_iso(created, a) || !parse_iso(completed, b)) return L"\u2014";
    FILETIME fa, fb;
    if (!SystemTimeToFileTime(&a, &fa) || !SystemTimeToFileTime(&b, &fb)) return L"\u2014";
    ULARGE_INTEGER ua, ub;
    ua.LowPart = fa.dwLowDateTime; ua.HighPart = fa.dwHighDateTime;
    ub.LowPart = fb.dwLowDateTime; ub.HighPart = fb.dwHighDateTime;
    __int64 diff = (__int64)((ub.QuadPart - ua.QuadPart) / 10000000ULL); // seconds
    if (diff < 0) diff = 0;
    long totalH = (long)(diff / 3600.0 + 0.5); // round to hours
    if (totalH < 1) return L"\u4e0d\u8db31\u5c0f\u65f6"; // 不足1小时
    long days = totalH / 24, hrs = totalH % 24;
    wchar_t buf[64] = {};
    if (days > 0 && hrs > 0) swprintf_s(buf, L"%ld\u5929%ld\u5c0f\u65f6", days, hrs);
    else if (days > 0) swprintf_s(buf, L"%ld\u5929", days);
    else swprintf_s(buf, L"%ld\u5c0f\u65f6", hrs);
    return buf;
}

bool write_utf8_file(const std::wstring& filename, const std::wstring& text) {
    return write_file(exe_dir() + filename, w_to_utf8(text));
}

bool is_auto_start() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    wchar_t buf[1024] = {0}; DWORD sz = sizeof(buf);
    LONG r = RegQueryValueExW(hKey, L"LvZhiFangTodo", nullptr, nullptr, (BYTE*)buf, &sz);
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

void set_auto_start(bool en) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) return;
    if (en) {
        wchar_t path[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring val = L"\"" + std::wstring(path) + L"\"";
        RegSetValueExW(hKey, L"LvZhiFangTodo", 0, REG_SZ,
                       (BYTE*)val.c_str(), (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, L"LvZhiFangTodo");
    }
    RegCloseKey(hKey);
}
