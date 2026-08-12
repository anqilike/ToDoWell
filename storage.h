#pragma once
#include <string>
#include <vector>

struct TodoItem { std::wstring text; bool done = false; std::wstring created; }; // created: ISO yyyy-MM-ddTHH:mm:ss
struct Project { std::wstring name; std::vector<TodoItem> todos; };

// JSON data files live next to the executable.
std::vector<Project> load_projects();
void save_projects(const std::vector<Project>& projs);

struct Config {
    std::wstring title_prefix = L"\u4f60\u597d"; // 你好
    bool auto_start = false;
    int snap_anim = 1;
    int default_pos = 0; // 0=右下角 1=左上角 2=左下角 3=右上角
};
// Snap animation modes: 0=instant, 1=fly-out/slide-in, 2=portal fade, 3=elastic, 4=arc glide.
// Snap animation modes: 0=instant, 1=portal fade, 2=elastic spring, 3=arc glide.
Config load_config();
void save_config(const Config& cfg);

struct HistoryItem { std::wstring project, text, created, completed; };
std::vector<HistoryItem> load_history();
void save_history(const std::vector<HistoryItem>& items);

// Time helpers.
std::wstring now_iso();
std::wstring iso_to_display(const std::wstring& iso);
std::wstring duration_text(const std::wstring& created, const std::wstring& completed);

// Write a UTF-8 text file next to the executable (used by the history page).
bool write_utf8_file(const std::wstring& filename, const std::wstring& text);

// HKCU Run-key autostart, key name LvZhiFangTodo.
bool is_auto_start();
void set_auto_start(bool en);

// UTF-8 <-> UTF-16 helpers.
std::wstring utf8_to_w(const std::string& s);
std::string w_to_utf8(const std::wstring& ws);
// Folder containing the running exe (trailing backslash).
std::wstring exe_dir();
