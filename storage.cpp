#include "storage.h"
#include "json.h"
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <sstream>

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
                if (tx && tx->type == JsonValue::Str) t.text = tx->str;
                if (dn && dn->type == JsonValue::Bool) t.done = dn->b;
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
            to.obj.emplace_back(L"text", tx);
            to.obj.emplace_back(L"done", dn);
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
    return cfg;
}

void save_config(const Config& cfg) {
    JsonValue root; root.type = JsonValue::Obj;
    JsonValue tp; tp.type = JsonValue::Str; tp.str = cfg.title_prefix;
    JsonValue as; as.type = JsonValue::Bool; as.b = cfg.auto_start;
    JsonValue sa; sa.type = JsonValue::Num; sa.num = (double)cfg.snap_anim;
    root.obj.emplace_back(L"title_prefix", tp);
    root.obj.emplace_back(L"auto_start", as);
    root.obj.emplace_back(L"snap_anim", sa);
    write_file(exe_dir() + L"config.json", w_to_utf8(json_stringify(root, false)));
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
