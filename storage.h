#pragma once
#include <string>
#include <vector>

struct TodoItem { std::wstring text; bool done = false; };
struct Project { std::wstring name; std::vector<TodoItem> todos; };

// JSON data files live next to the executable.
std::vector<Project> load_projects();
void save_projects(const std::vector<Project>& projs);

struct Config { std::wstring title_prefix = L"\u4f60\u597d"; bool auto_start = false; int snap_anim = 1; };
// Snap animation modes: 0=instant, 1=fly-out/slide-in, 2=portal fade, 3=elastic, 4=arc glide.
// Snap animation modes: 0=instant, 1=portal fade, 2=elastic spring, 3=arc glide.
Config load_config();
void save_config(const Config& cfg);

// HKCU Run-key autostart, key name LvZhiFangTodo.
bool is_auto_start();
void set_auto_start(bool en);

// UTF-8 <-> UTF-16 helpers.
std::wstring utf8_to_w(const std::string& s);
std::string w_to_utf8(const std::wstring& ws);
// Folder containing the running exe (trailing backslash).
std::wstring exe_dir();
