#pragma once
#include "gfx.h"
#include "storage.h"
#include <vector>
#include <string>

// Layout constants, in DIPs (1 DIP = 1/96"; render target maps to physical px via DPI).
namespace AppC {
constexpr float TITLE_H = 38.0f;
constexpr float BOT_H = 38.0f;
constexpr float RESIZE_M = 5.0f;
constexpr float CORNER_R = 8.0f;
constexpr float MIN_W = 240.0f;  // was 320, scaled down to match the smaller default
constexpr float MIN_H = 320.0f;  // was 400, scaled down to match the smaller default
constexpr float RATIO_H_W = 1.5f; // height = RATIO_H_W * width  (3:2)
constexpr float CONTENT_PAD = 10.0f;   // left/right content margin
constexpr float CONTENT_TOP = 8.0f;    // top padding inside content
constexpr float CARD_GAP = 10.0f;      // gap between cards
constexpr float CARD_INNER = 14.0f;    // card internal left/right padding
constexpr float CARD_TOP = 10.0f;      // card top padding
constexpr float ROW_H = 26.0f;         // todo row height
constexpr float CIRCLE_R = 7.0f;       // todo circle radius
constexpr float BADGE_H = 18.0f;       // project number badge height
constexpr float BADGE_MIN_W = 22.0f;   // project number badge min width
constexpr float SNAP_GAP = 15.0f;      // edge-snap threshold (px)
constexpr float SCROLL_STEP = 32.0f;   // DIPs per wheel notch
}

enum EditMode { ED_NONE, ED_ADD_PROJECT, ED_EDIT_PROJECT, ED_NEW_TODO, ED_EDIT_TODO, ED_PREF_PREFIX };

enum HitType {
    H_NONE, H_TITLEBAR, H_SNAP, H_CLOSE, H_ADD, H_GEAR,
    H_PROJ_NAME, H_PROJ_DEL, H_TODO_CIRCLE, H_TODO_TEXT, H_NEWTODO,
    H_OVERLAY_SHADE, H_SETTINGS_PANEL, H_CHECKBOX, H_PREF_FIELD, H_ABOUT_BTN,
    H_ABOUT_PANEL, H_ABOUT_CLOSE
};
struct Hit { D2D1_RECT_F rc; HitType type; int pi = -1; int ti = -1; };

struct FadeTodo { int pi; int ti; float alpha; float off; }; // completing todo anim

class App {
public:
    void create(HWND hwnd);
    void destroy();
    void tick(float dt);
    void render();
    void onSize();
    LRESULT onNcHitTest(POINT screenPt);
    void onMouseMove(float xdip, float ydip);
    void onLButtonDown(float xdip, float ydip);
    void onLButtonUp(float xdip, float ydip);
    void onLButtonDblClk(float xdip, float ydip);
    void onWheel(int delta);
    void onChar(wchar_t ch);
    void onKeyDown(int vk);
    void onMoving(RECT* r);
    void onSizing(RECT* r);
    void onMinMax(MINMAXINFO* mm);
    // Edit control callbacks (invoked by subclass proc).
    void onEditReturn();
    void onEditEscape();
    void onEditKillFocus();
    void positionIME();
    void positionIMELight(); // update screen coords + ImmSet windows, no rebuildHits
    void onCompositionUpdate(const std::wstring& s);
    void onCompositionResult(const std::wstring& s);
    void onCompositionEnd();
    HWND hwnd() const { return m_hwnd; }
    HWND editHwnd() const { return m_edit; }
    POINT imePos() const { return m_imePos; }

    float toDip(int px) const;
    int toPx(float dip) const;
    bool isClosing() const { return m_closing; }
    bool isEditing() const { return m_editMode != ED_NONE; }
    void positionEdit();

private:
    HWND m_hwnd = nullptr;
    HWND m_edit = nullptr;
    WNDPROC m_editOldProc = nullptr;
    bool m_reentering = false; // suppress killfocus cancel during re-focus
    bool m_caretCreated = false; // track caret lifecycle

    std::vector<Project> m_projects;
    Config m_cfg;

    // Window size in DIPs (client area).
    float m_w = 0, m_h = 0;

    // Scroll (DIPs), smoothed.
    float m_scroll = 0, m_scrollTarget = 0, m_contentH = 0;

    // Edit state.
    EditMode m_editMode = ED_NONE;
    int m_editPi = -1, m_editTi = -1;

    // New-project placeholder flag.
    bool m_addingProject = false;

    // Overlays.
    bool m_settings = false;
    float m_setScroll = 0, m_setScrollTarget = 0, m_setContentH = 0;
    float m_aboutScroll = 0, m_aboutScrollTarget = 0, m_aboutContentH = 0;
    bool m_about = false;
    bool m_closing = false;
    std::wstring m_editText;
    std::wstring m_compositionText; // in-progress IME composition
    float m_cursorBlink = 0; // cursor blink timer
    float m_closeAnim = 0; // 0..1 close animation progress
    int m_closeFrames = 0;
    bool m_snapping = false;
    bool m_snappedToTarget = false;
    float m_snapScale = 1.0f;
    float m_snapAnim = 0;
    int m_snapFromX = 0, m_snapFromY = 0, m_snapToX = 0, m_snapToY = 0;
    bool m_snapRightArmed = false; bool m_snapBottomArmed = false;
    bool m_snapLeftArmed = true; bool m_snapTopArmed = true;
    float m_overlayAlpha = 0;     // fade for settings shade
    float m_aboutAlpha = 0;

    // Hover state (for color lerp).
    int m_hoverSnap = 0; float m_snapT = 0;
    int m_hoverClose = 0; float m_closeT = 0;
    int m_hoverAdd = 0; float m_addT = 0;
    int m_hoverGear = 0; float m_gearT = 0;
    int m_hoverProjDel = -1; float m_projDelT = 0;
    // circle hover: store pi,ti
    int m_hovCircPi = -1, m_hovCircTi = -2; float m_circT = 0; // ti=-2 means new-todo circle
    bool m_suppressCircleHover = false; // suppress hover after click until mouse moves
    float m_lastMouseX = -1, m_lastMouseY = -1; // for detecting real mouse movement
    int m_hoverCheckbox = 0; float m_checkT = 0;
    int m_hoverAboutBtn = 0; float m_aboutBtnT = 0;

    std::vector<FadeTodo> m_fades;
    std::vector<Hit> m_hits; // content-space rects (y relative to content top, pre-scroll)
    D2D1_RECT_F m_editRectDip = {};
    POINT m_imePos = {}; // screen-space DIP rect where EDIT should sit

    void rebuildHits();
    void beginEdit(EditMode mode, int pi, int ti, const std::wstring& initial);
    void endEdit(bool applyFocus);
    void ensureEditCreated();
    std::wstring editGetText();
    void editSetText(const std::wstring& s);
    void commitEdit();
    void cancelEdit();
    void addProjectCommit(const std::wstring& name);
    void focusNewTodo(int pi);
    void completeTodo(int pi, int ti);
    void delProject(int pi);
    void openSettings();
    void closeSettings();
    void openAbout();
    void closeAbout();
    void requestClose();
    void requestRedraw();
    bool animating() const;
    float lerp(float a, float b, float t) const { return a + (b - a) * t; }
    void approach(float& cur, float target, float dt, float speed);
    D2D1_COLOR_F lerpColor(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) const;
    void clampScroll();
    void saveAll();
};

extern App* g_app;
LRESULT CALLBACK EditSubproc(HWND, UINT, WPARAM, LPARAM);
