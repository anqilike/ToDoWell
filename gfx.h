#pragma once
#define NOMINMAX
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <windows.h>

template <typename T> inline void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

inline D2D1_COLOR_F Hex(DWORD h, float a = 1.0f) {
    return D2D1::ColorF(((h >> 16) & 0xFF) / 255.0f, ((h >> 8) & 0xFF) / 255.0f, (h & 0xFF) / 255.0f, a);
}

// Palette (matches the Python spec).
namespace C {
const D2D1_COLOR_F BG        = Hex(0xffffff);
const D2D1_COLOR_F PAGE      = Hex(0xf5f5f7);
const D2D1_COLOR_F TEXT      = Hex(0x1d1d1f);
const D2D1_COLOR_F MUTED     = Hex(0x86868b);
const D2D1_COLOR_F ACCENT    = Hex(0x0066cc);
const D2D1_COLOR_F SEP       = Hex(0xe5e5e5);
const D2D1_COLOR_F CIRCLE    = Hex(0xc6c6c8);
const D2D1_COLOR_F TITLE_BG  = Hex(0xFFEAA7);
const D2D1_COLOR_F TITLE_FG  = Hex(0x2c2c2e);
const D2D1_COLOR_F INPUT_BD  = Hex(0xd1d1d6);
const D2D1_COLOR_F DEL_BTN   = Hex(0xc6c6c8);
const D2D1_COLOR_F DEL_HOVER = Hex(0xff3b30);
const D2D1_COLOR_F WHITE     = Hex(0xffffff);
const D2D1_COLOR_F DIALOG_BG = Hex(0xf0f0f0);
const D2D1_COLOR_F DIALOG_TX = Hex(0x333333);
const D2D1_COLOR_F DIALOG_MM = Hex(0x999999);
const D2D1_COLOR_F DIALOG_AB = Hex(0x555555);
const D2D1_COLOR_F DIALOG_FT = Hex(0xaaaaaa);
const D2D1_COLOR_F CLEAR     = D2D1::ColorF(0, 0, 0, 0);
const DWORD PROJ_COLORS[8] = {0xe74c3c, 0x3498db, 0x2ecc71, 0xf39c12, 0x9b59b6, 0x1abc9c, 0xe67e22, 0x34495e};
}

enum FontId {
    F_TITLE_BIG,   // YaHei 16 bold  (gogogo!!!)
    F_TITLE_PRE,   // YaHei 12 bold  (prefix)
    F_PROJ_NAME,   // YaHei 12 bold  (project name)
    F_PROJ_NUM,    // YaHei 9 bold   (project number badge)
    F_TODO,        // YaHei 10       (todo text)
    F_TODO_PH,     // YaHei 10       (placeholder, same metrics)
    F_SETTINGS,    // YaHei 10       (settings labels)
    F_HINT,        // YaHei 9        (small hints / about features)
    F_ABOUT_TITLE, // YaHei 14 bold  (about title)
    F_FOOTER,      // YaHei 8        (version footer)
    F_SYM_TITLE,   // symbol 11      (title bar -> and close)
    F_SYM_BOTTOM,  // symbol 17      (bottom + and gear)
    F_SYM_CIRCLE,  // symbol 16      (todo circle o)
    F_SYM_SMALL,   // symbol 9       (badge delete x / check)
    F_SYM_CHECK,   // symbol 12      (checkbox mark)
    F_COUNT
};

class Gfx {
public:
    HWND hwnd = nullptr;
    ID2D1Factory* d2d = nullptr;
    IDWriteFactory* dw = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    IDWriteTextFormat* fonts[F_COUNT] = {};
    float dpiScale = 1.0f;

    bool init(HWND hwnd);
    bool recreate(HWND hwnd);
    void resize();
    void cleanup();
    IDWriteTextFormat* font(FontId id);
    // pt -> DIPs (96/72).
    static float pt(float p) { return p * 96.0f / 72.0f; }
    // DIPs -> physical px for window creation.
    float dip(float d) const { return d * dpiScale; }

    void setColor(D2D1_COLOR_F c) { brush->SetColor(c); }
    void fillRect(D2D1_RECT_F rc, D2D1_COLOR_F c);
    void fillRoundedRect(D2D1_RECT_F rc, float r, D2D1_COLOR_F c);
    void strokeRect(D2D1_RECT_F rc, D2D1_COLOR_F c, float w = 1.0f);
    void strokeRoundedRect(D2D1_RECT_F rc, float r, D2D1_COLOR_F c, float w = 1.0f);
    void fillEllipse(float cx, float cy, float rx, float ry, D2D1_COLOR_F c);
    void drawEllipse(float cx, float cy, float rx, float ry, D2D1_COLOR_F c, float w = 1.5f);
    void drawLine(float x1, float y1, float x2, float y2, D2D1_COLOR_F c, float w = 1.0f);
    void drawText(const std::wstring& s, D2D1_RECT_F rc, FontId fid, D2D1_COLOR_F c,
                  DWRITE_TEXT_ALIGNMENT ta = DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT pa = DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    // Measure the width a string would occupy with the given font.
    float measureTextW(const std::wstring& s, FontId fid, float maxWidth = 10000.0f);
    // Client size in DIPs (render target is DPI-aware, so GetSize is in DIPs).
    float clientW() const { return rt ? rt->GetSize().width : 0; }
    float clientH() const { return rt ? rt->GetSize().height : 0; }
};

extern Gfx g_gfx;
