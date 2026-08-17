#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <imm.h>
#include <shellapi.h>
#include <cstdarg>
#include "gfx.h"
#include "app.h"
#include "resource.h"
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "shell32.lib")

static WNDPROC g_editOldProc = nullptr;
static HFONT g_fontTodo = nullptr;
static HFONT g_fontProj = nullptr;
static HBRUSH g_whiteBrush = nullptr;
// 版权署名：公开版为 anqilike；编译内部版时把这里改为 5014 即可全局生效。
static const wchar_t* kCopyright = L"\u7248\u6743\u6240\u6709@\u5929\u624d\u7684anqilike";

App* g_app = nullptr;

float App::toDip(int px) const { return g_gfx.dpiScale > 0 ? px / g_gfx.dpiScale : (float)px; }
int App::toPx(float dip) const { return (int)(dip * g_gfx.dpiScale + 0.5f); }
void App::approach(float& cur, float target, float dt, float speed) {
    cur += (target - cur) * std::min(1.0f, dt * speed);
}
void App::requestRedraw() { if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE); }

void App::clampScroll() {
    float mx = m_contentH - (m_h - AppC::TITLE_H - AppC::BOT_H);
    if (mx < 0) mx = 0;
    if (m_scrollTarget < 0) m_scrollTarget = 0;
    if (m_scrollTarget > mx) m_scrollTarget = mx;
    if (m_scroll < 0) m_scroll = 0;
    if (m_scroll > mx) m_scroll = mx;
}
void App::saveAll() { save_projects(m_projects); save_config(m_cfg); }
void App::requestClose() {
    if (m_closing) return;
    saveAll();
    m_closing = true;
    m_closeAnim = 0;
    m_closeFrames = 0;
    requestRedraw();
}
void App::showFromTray(bool openSettingsPanel) {
    ShowWindow(m_hwnd, SW_SHOW);
    bringToFront();
    SetForegroundWindow(m_hwnd);
    if (openSettingsPanel && !m_settings) openSettings();
    requestRedraw();
}
void App::bringToFront() {
    // Windows 10 can leave a WS_EX_TOOLWINDOW (no taskbar button) below the
    // desktop icon layer (SHELLDLL_DefView) when the window is created while
    // Explorer is still building the desktop, e.g. at auto-start. Moving to
    // TOPMOST and immediately back to NOTOPMOST lifts it above the icon layer
    // without keeping it always-on-top.
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(m_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
void App::exitFromTray() {
    requestClose();
}
bool App::animating() const {
    if (m_closing) return true;
    if (m_snapping) return true;
    // Keep repainting while the overlay shade fades in OR out, so the main
    // page never gets stuck dimmed after closing settings/about.
    if (m_overlayAlpha > 0.01f) return true;
    if (m_aboutAlpha > 0.01f) return true;
    if ((m_addSpin > 0.001f && m_addSpin < 1.0f) ||
        (m_gearSpin > 0.001f && m_gearSpin < 1.0f) ||
        (m_projDelSpin > 0.001f && m_projDelSpin < 1.0f)) return true;
    if (m_circT > 0.001f && m_circT < 1.0f) return true;
    if (!m_fades.empty()) return true;
    return false;
}
static HFONT makeHFont(int pt, bool bold) {
    HDC hdc = GetDC(nullptr);
    int h = -MulDiv(pt, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, hdc);
    return CreateFontW(h, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
}
void App::create(HWND hwnd) {
    g_app = this;
    m_hwnd = hwnd;
    if (!g_whiteBrush) g_whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    m_projects = load_projects();
    if (!m_projects.empty()) {
        for (auto& p : m_projects) {
            if (p.name.empty()) p.name = L"\u9ed8\u8ba4\u9879\u76ee";
        }
    }
    m_cfg = load_config();
    m_cfg.auto_start = is_auto_start();
    m_w = g_gfx.clientW();
    m_h = g_gfx.clientH();
    refreshImeMode(); // 打开时立即识别当前输入法（搜狗走传统路径，微软拼音走自绘路径）
    if (!g_fontTodo) g_fontTodo = makeHFont(10, false);
    if (!g_fontProj) g_fontProj = makeHFont(12, true);
    rebuildHits();
    clampScroll();
}
void App::destroy() {
    saveAll();
    if (g_whiteBrush) { DeleteObject(g_whiteBrush); g_whiteBrush = nullptr; }
    if (g_fontTodo) { DeleteObject(g_fontTodo); g_fontTodo = nullptr; }
    if (g_fontProj) { DeleteObject(g_fontProj); g_fontProj = nullptr; }
}
void App::ensureEditCreated() {
    if (m_edit) return;
    m_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL,
                             0, 0, 1, 1, m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (m_edit) {
        g_editOldProc = (WNDPROC)SetWindowLongPtrW(m_edit, GWLP_WNDPROC, (LONG_PTR)EditSubproc);
        m_editOldProc = g_editOldProc;
        SendMessageW(m_edit, WM_SETFONT, (WPARAM)g_fontTodo, TRUE);
        // Clip the EDIT's own drawing to 1 pixel: the IME/EDIT may paint the
        // composition into its DC (bypassing WM_PAINT), which would overlap
        // the D2D text. The window rect stays full-size for TSF geometry.
        HRGN rgn = CreateRectRgn(0, 0, 1, 1);
        SetWindowRgn(m_edit, rgn, TRUE);
    }
}
// The IME draws the in-progress pinyin in its own floating window
// ("MSCTFIME Composition"). The app renders the composition itself via D2D,
// so this system window is hidden to keep the input visually integrated.
static void HideImeCompositionWindowsInternal() {
    EnumWindows([](HWND h, LPARAM) -> BOOL {
        wchar_t cls[64];
        if (GetClassNameW(h, cls, 64) > 0 && wcscmp(cls, L"MSCTFIME Composition") == 0) {
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid == GetCurrentProcessId()) ShowWindow(h, SW_HIDE);
        }
        return TRUE;
    }, 0);
}
// Hide the IME's own composition/candidate windows for modern TSF IMEs
// (Microsoft Pinyin), which expose candidate data through the IMM bridge so
// the app can render its own integrated UI. Legacy IMEs (e.g. Sogou on
// Windows 7) keep their own windows visible.
void App::hideImeWindows() {
    if (m_imeLegacy) return;
    // Re-check the active IME before hiding anything: legacy IMEs (Sogou,
    // other third-party IMEs) must keep their own windows, and hiding them
    // at composition start is what made the candidate box disappear.
    if (detectLegacyIme()) {
        m_imeLegacy = true;
        return;
    }
    HideImeCompositionWindowsInternal();
}
bool App::detectLegacyIme() {
    HKL hkl = GetKeyboardLayout(0);
    wchar_t desc[128] = {};
    if (ImmGetDescriptionW(hkl, desc, 128) > 0) {
        std::wstring s(desc);
        // Microsoft built-in Pinyin exposes candidates through the IMM bridge
        // and works with the app-rendered candidate popup.
        if (s.find(L"Microsoft") != std::wstring::npos) return false;
        if (s.find(L"\u5fae\u8f6f\u62fc\u97f3") != std::wstring::npos) return false; // 微软拼音
        // Sogou and other third-party IMEs manage their own candidate UI and
        // often do not expose candidate data -> keep their windows visible.
        if (s.find(L"\u641c\u72d7") != std::wstring::npos ||   // 搜狗
            s.find(L"Sogou") != std::wstring::npos ||
            s.find(L"sogou") != std::wstring::npos) return true;
        return true; // any other non-Microsoft IME -> legacy path
    }
    return false;
}
void App::refreshImeMode() {
    m_imeLegacy = detectLegacyIme();
}
LRESULT CALLBACK EditSubproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) { if (g_app) g_app->onEditReturn(); return 0; }
        if (wp == VK_ESCAPE) { if (g_app) g_app->onEditEscape(); return 0; }
    } else if (msg == WM_CHAR) {
        if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;
    } else if (msg == WM_GETDLGCODE) {
        return DLGC_WANTALLKEYS;
    } else if (msg == WM_KILLFOCUS) {
        if (g_app) g_app->onEditKillFocus();
    } else if (msg == WM_SETFOCUS) {
        LRESULT res = CallWindowProcW(g_editOldProc, h, msg, wp, lp);
        // The EDIT paints nothing; D2D draws the visible caret.
        HideCaret(h);
        return res;
    } else if (msg == WM_PAINT) {
        PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps);
        return 0;
    } else if (msg == WM_ERASEBKGND) {
        return 1;
    } else if (msg == WM_PRINTCLIENT) {
        return 0;
    } else if (msg == WM_IME_SETCONTEXT) {
        // Hide the IME's own composition/guideline windows; the app renders
        // its own input UI. Candidate data is kept so the built-in popup works.
        LPARAM flags = lp & ~(ISC_SHOWUICOMPOSITIONWINDOW | ISC_SHOWUIGUIDELINE);
        LRESULT res = CallWindowProcW(g_editOldProc, h, msg, wp, flags);
        if (g_app) g_app->hideImeWindows();
        return res;
    } else if (msg == WM_IME_STARTCOMPOSITION) {
        if (g_app) g_app->hideImeWindows();
        return CallWindowProcW(g_editOldProc, h, msg, wp, lp);
    } else if (msg == WM_IME_COMPOSITION) {
        if (g_app) g_app->hideImeWindows();
        if (g_app && g_app->isEditing()) {
            HIMC himc = ImmGetContext(h);
            if (himc) {
                if (lp & GCS_RESULTSTR) {
                    LONG len = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
                    if (len > 0) {
                        std::wstring result(len / 2, L'\0');
                        ImmGetCompositionStringW(himc, GCS_RESULTSTR, &result[0], len);
                        while (!result.empty() && result.back() == 0) result.pop_back();
                        g_app->onCompositionResult(result);
                    }
                }
                if (lp & GCS_COMPSTR) {
                    LONG len = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
                    std::wstring comp;
                    if (len > 0) {
                        comp.resize(len / 2);
                        ImmGetCompositionStringW(himc, GCS_COMPSTR, &comp[0], len);
                        while (!comp.empty() && comp.back() == 0) comp.pop_back();
                    }
                    g_app->onCompositionUpdate(comp);
                }
                ImmReleaseContext(h, himc);
            }
        }
        return CallWindowProcW(g_editOldProc, h, msg, wp, lp);
    } else if (msg == WM_IME_ENDCOMPOSITION) {
        if (g_app) g_app->onCompositionEnd();
        return CallWindowProcW(g_editOldProc, h, msg, wp, lp);
    } else if (msg == WM_IME_NOTIFY) {
        return CallWindowProcW(g_editOldProc, h, msg, wp, lp);
    }
    return CallWindowProcW(g_editOldProc, h, msg, wp, lp);
}
std::wstring App::editGetText() {
    if (!m_edit) return L"";
    int n = GetWindowTextLengthW(m_edit);
    std::wstring s(n, L'\0');
    if (n > 0) GetWindowTextW(m_edit, &s[0], n + 1);
    return s;
}
void App::editSetText(const std::wstring& s) {
    if (!m_edit) return;
    SetWindowTextW(m_edit, s.c_str());
    int len = (int)s.size();
    SendMessageW(m_edit, EM_SETSEL, len, len); // caret at end, no selection
}
void App::beginEdit(EditMode mode, int pi, int ti, const std::wstring& initial) {
    m_editMode = mode; m_editPi = pi; m_editTi = ti;
    m_editText = initial;
    m_compositionText.clear();
    m_cursorBlink = 0;
    if (mode == ED_PREF_PREFIX) {
        float dlgW = 220.0f, dlgH = 230.0f;
        float dlgX = (m_w - dlgW) / 2.0f, dlgY = (m_h - dlgH) / 2.0f;
        float sy = -m_setScroll;
        float cy = dlgY + 14.0f + sy, cb = 18.0f;
        float ly = cy + cb + 8.0f;
        float fy = ly + 18.0f;
        m_editRectDip = D2D1::RectF(dlgX + 16, fy, dlgX + dlgW - 16, fy + 26);
    } else {
        rebuildHits();
    }
    ensureEditCreated();
    editSetText(initial);
    if (m_edit) {
        HFONT f = (mode == ED_EDIT_PROJECT || mode == ED_ADD_PROJECT) ? g_fontProj : g_fontTodo;
        SendMessageW(m_edit, WM_SETFONT, (WPARAM)f, TRUE);
    }
    positionEdit();
    if (m_edit) { SetFocus(m_edit); }
    requestRedraw();
}
void App::startEdit(EditMode mode, int pi, int ti, const std::wstring& initial) {
    // If a different field is being edited, commit it first so typed changes
    // are never silently discarded when the user clicks another editable row.
    if (m_editMode != ED_NONE && m_editMode != ED_PREF_PREFIX &&
        !(m_editMode == mode && m_editPi == pi && m_editTi == ti)) {
        commitEdit();
    }
    beginEdit(mode, pi, ti, initial);
}
void App::endEdit(bool applyFocus) {
    m_reentering = true;
    m_editMode = ED_NONE; m_editPi = -1; m_editTi = -1;
    m_compositionText.clear();
    m_lastEditLayout.clear();
    m_cands.clear();
    m_candSel = -1;
    m_reentering = false;
    (void)applyFocus;
    if (m_edit) {
        ShowWindow(m_edit, SW_HIDE);
        m_editPosX = m_editPosY = m_editPosW = m_editPosH = -1;
    }
    requestRedraw();
}
float App::editUnderlineDip() const {
    if (m_editMode == ED_PREF_PREFIX || m_editRectDip.right <= m_editRectDip.left)
        return -1.0f;
    switch (m_editMode) {
        case ED_EDIT_PROJECT: return m_editRectDip.bottom - 3.0f; // BADGE_H row
        case ED_ADD_PROJECT:  return m_editRectDip.bottom;        // 30px new-project line
        default:              return m_editRectDip.bottom - 2.0f; // todo rows
    }
}
int App::editTextHeightPx() const {
    if (!m_edit) return toPx(AppC::ROW_H);
    HFONT f = (m_editMode == ED_EDIT_PROJECT || m_editMode == ED_ADD_PROJECT)
              ? g_fontProj : g_fontTodo;
    HDC hdc = GetDC(m_edit);
    if (!hdc) return toPx(AppC::ROW_H);
    HGDIOBJ old = SelectObject(hdc, f);
    TEXTMETRICW tm = {};
    int h = toPx(AppC::ROW_H);
    if (GetTextMetricsW(hdc, &tm) && tm.tmHeight > 0) h = (int)tm.tmHeight;
    SelectObject(hdc, old);
    ReleaseDC(m_edit, hdc);
    return h;
}
void App::positionEdit() {
    if (!m_edit || m_editMode == ED_NONE) return;
    if (m_editRectDip.left == 0 && m_editRectDip.right == 0) return;
    // Keep the hidden EDIT's text bottom on the visible underline so the
    // TSF caret/IME geometry uses the same line as the UI.
    float editW = std::max(40.0f, m_editRectDip.right - m_editRectDip.left);
    float editTop;
    float editH;
    if (m_editMode == ED_PREF_PREFIX) {
        editTop = m_editRectDip.top;
        editH = std::max(AppC::ROW_H, m_editRectDip.bottom - m_editRectDip.top);
    } else {
        float underline = editUnderlineDip() + AppC::TITLE_H - m_scroll;
        editH = toDip(editTextHeightPx());
        editTop = underline - editH;
    }
    int ex = toPx(m_editRectDip.left), ey = toPx(editTop);
    int ew = toPx(editW), eh = toPx(editH);
    // Avoid repositioning every tick (1ms timer): a SetWindowPos storm disturbs
    // the IME's caret-geometry queries. Only move when something changed.
    if (ex != m_editPosX || ey != m_editPosY || ew != m_editPosW || eh != m_editPosH) {
        SetWindowPos(m_edit, nullptr, ex, ey, ew, eh, SWP_NOZORDER | SWP_NOACTIVATE);
        m_editPosX = ex; m_editPosY = ey; m_editPosW = ew; m_editPosH = eh;
    }
    ShowWindow(m_edit, SW_SHOWNOACTIVATE);
    HideCaret(m_edit); // D2D draws the visible caret
}
// Returns caret geometry relative to a text layout that matches how the text is
// drawn (same width/height, vertically centered): x from the layout's left edge
// and the top/height of the caret's line.
static bool editCaretGeometry(const std::wstring& text, int pos, float maxW, float maxH, FontId fid,
                              float& relX, float& relTop, float& relH) {
    if (text.empty() || maxW <= 0 || maxH <= 0) return false;
    IDWriteTextFormat* fmt = g_gfx.font(fid);
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    IDWriteTextLayout* lay = nullptr;
    if (FAILED(g_gfx.dw->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                                          fmt, maxW, maxH, &lay)) || !lay)
        return false;
    DWRITE_TEXT_METRICS tm = {};
    DWRITE_HIT_TEST_METRICS htm = {};
    FLOAT hx = 0, hy = 0;
    bool ok = SUCCEEDED(lay->GetMetrics(&tm)) && tm.height > 0 &&
              SUCCEEDED(lay->HitTestTextPosition((UINT32)std::min(pos, (int)text.size()),
                                                 FALSE, &hx, &hy, &htm)) &&
              htm.height > 0;
    if (ok) {
        relX = hx; relTop = htm.top; relH = htm.height;
    }
    lay->Release();
    return ok;
}
// Converts a point inside the text layout (relative to its top-left) to a
// character index, matching how DirectWrite places the caret on click.
static int editIndexFromPoint(const std::wstring& text, float x, float y, float maxW, float maxH, FontId fid) {
    if (text.empty() || maxW <= 0 || maxH <= 0) return 0;
    IDWriteTextFormat* fmt = g_gfx.font(fid);
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    IDWriteTextLayout* lay = nullptr;
    if (FAILED(g_gfx.dw->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                                          fmt, maxW, maxH, &lay)) || !lay)
        return 0;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS htm = {};
    HRESULT hr = lay->HitTestPoint(x, y, &trailing, &inside, &htm);
    lay->Release();
    if (FAILED(hr)) return 0;
    int pos = (int)htm.textPosition;
    if (trailing) ++pos;
    if (pos < 0) pos = 0;
    if (pos > (int)text.size()) pos = (int)text.size();
    return pos;
}
int App::editCaretPos() const {
    if (!m_edit) return (int)m_editText.size();
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(m_edit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    return (int)selEnd;
}
void App::setEditCaretFromPoint(float x, float y) {
    if (!m_edit || m_editMode == ED_NONE) return;
    if (!m_compositionText.empty()) return; // IME owns the caret while composing
    FontId fid = (m_editMode == ED_EDIT_PROJECT || m_editMode == ED_ADD_PROJECT) ? F_PROJ_NAME : F_TODO;
    float maxW = m_editRectDip.right - m_editRectDip.left;
    float maxH = m_editRectDip.bottom - m_editRectDip.top;
    if (maxW <= 0 || maxH <= 0) return;
    int pos = editIndexFromPoint(m_editText, x - m_editRectDip.left, y - m_editRectDip.top, maxW, maxH, fid);
    SendMessageW(m_edit, EM_SETSEL, (WPARAM)pos, (LPARAM)pos);
    m_cursorBlink = 0;
    requestRedraw();
}
void App::drawEditCaret(Gfx& g, FontId fid, const std::wstring& text,
                        float left, float top, float width, float height) {
    if (((int)(m_cursorBlink * 2) % 2) != 0) return; // blink off
    float cx = left + g.measureTextW(text, fid) + 1;   // fallback: end of text
    float cy = top + 2;
    float ch = height - 4;
    if (m_compositionText.empty()) {
        float relX = 0, relTop = 0, relH = 0;
        if (editCaretGeometry(text, editCaretPos(), width, height, fid, relX, relTop, relH)) {
            cx = left + relX;
            cy = top + relTop;
            ch = relH;
        }
    }
    g.drawLine(cx, cy + 1, cx, cy + ch - 1, C::ACCENT, 1.5f);
}
// The system IME (Microsoft Pinyin compatibility mode) draws its composition
// and candidate list in its own floating window. That window is hidden, so
// this polls the IME state and the app renders the pinyin and candidates
// itself (fully integrated with the UI).
void App::pollIme() {
    if (!m_edit || m_editMode == ED_NONE) return;
    HIMC himc = ImmGetContext(m_edit);
    if (!himc) return;

    std::wstring comp;
    LONG len = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
    if (len > 0) {
        comp.resize(len / 2);
        ImmGetCompositionStringW(himc, GCS_COMPSTR, &comp[0], len);
        while (!comp.empty() && comp.back() == 0) comp.pop_back();
    }
    bool textChanged = (comp != m_compositionText);
    if (!comp.empty() && m_compositionText.empty()) {
        m_imeLegacy = detectLegacyIme(); // detect per composition session
    }
    m_compositionText = comp;
    if (textChanged) m_cursorBlink = 0;

    std::vector<std::wstring> cands;
    int sel = -1;
    if (!comp.empty()) {
        DWORD count = ImmGetCandidateListCountW(himc, 0);
        if (count > 0) {
            DWORD sz = ImmGetCandidateListW(himc, 0, nullptr, 0);
            if (sz > 0) {
                std::vector<BYTE> buf(sz);
                CANDIDATELIST* cl = (CANDIDATELIST*)buf.data();
                if (ImmGetCandidateListW(himc, 0, cl, sz)) {
                    sel = (int)cl->dwSelection;
                    for (DWORD i = 0; i < cl->dwCount; ++i) {
                        LPCWSTR s = (LPCWSTR)((BYTE*)cl + cl->dwOffset[i]);
                        cands.push_back(s);
                    }
                }
            }
        }
    }
    // Legacy path: IMEs like Sogou manage their own composition/candidate
    // windows and do not expose candidate data. Position their windows at the
    // caret with the classic Imm* calls (screen coordinates), which they honor.
    if (m_imeLegacy && !comp.empty()) {
        FontId fid = F_TODO;
        if (m_editMode == ED_EDIT_PROJECT || m_editMode == ED_ADD_PROJECT) fid = F_PROJ_NAME;
        else if (m_editMode == ED_PREF_PREFIX) fid = F_SETTINGS;
        float textW = g_gfx.measureTextW(m_editText + comp, fid);
        float px = m_editRectDip.left + textW;
        float py = m_editRectDip.bottom;
        if (m_editMode != ED_PREF_PREFIX) py += AppC::TITLE_H - m_scroll;
        POINT ptComp = { toPx(px), toPx(py) };
        POINT ptCand = { toPx(px), toPx(py + 4.0f) };
        // Legacy IMM IMEs (Sogou on Win7) expect CLIENT coordinates relative
        // to the application window; screen coordinates made the candidate
        // window clamp to the window edge or disappear entirely.
        COMPOSITIONFORM cf = {};
        cf.dwStyle = CFS_POINT;
        cf.ptCurrentPos = ptComp;
        ImmSetCompositionWindow(himc, &cf);
        CANDIDATEFORM cdf = {};
        cdf.dwIndex = 0;
        cdf.dwStyle = CFS_CANDIDATEPOS;
        cdf.ptCurrentPos = ptCand;
        cdf.rcArea = {0, 0, 0, 0};
        ImmSetCandidateWindow(himc, &cdf);
    }
    bool candChanged = (cands != m_cands || sel != m_candSel);
    m_cands = std::move(cands);
    m_candSel = sel;

    ImmReleaseContext(m_edit, himc);
    if (textChanged || candChanged) requestRedraw();
}
void App::cancelEdit() {
    if (m_addingProject) { m_addingProject = false; }
    endEdit(false);
}
void App::onEditKillFocus() {
    if (m_editMode == ED_NONE || m_reentering) return;
    if (m_editMode == ED_PREF_PREFIX) return; // handled by the settings click logic
    // If the user typed something and clicked away without Enter, auto-commit
    // the edit (like pressing Enter); an empty edit is simply cancelled.
    std::wstring t = editGetText();
    size_t a = t.find_first_not_of(L" \t\r\n");
    size_t b = t.find_last_not_of(L" \t\r\n");
    if (a != std::wstring::npos && b >= a) {
        commitEdit();
    } else {
        if (m_addingProject) m_addingProject = false;
        endEdit(false);
    }
}
void App::onEditReturn() { commitEdit(); }
void App::onEditEscape() {
    if (m_editMode == ED_PREF_PREFIX) { endEdit(false); closeSettings(); return; }
    if (m_editMode != ED_NONE) cancelEdit();
}
void App::commitEdit() {
    std::wstring t = editGetText();
    size_t a = t.find_first_not_of(L" \t\r\n");
    size_t b = t.find_last_not_of(L" \t\r\n");
    std::wstring trimmed = (a == std::wstring::npos) ? L"" : t.substr(a, b - a + 1);
    switch (m_editMode) {
        case ED_ADD_PROJECT: {
            if (!trimmed.empty()) addProjectCommit(trimmed);
            else { m_addingProject = false; endEdit(false); }
            break;
        }
        case ED_EDIT_PROJECT: {
            int pi = m_editPi;
            if (!trimmed.empty() && pi >= 0 && pi < (int)m_projects.size()) {
                m_projects[pi].name = trimmed;
                saveAll();
            }
            endEdit(false);
            if (pi >= 0 && pi < (int)m_projects.size()) focusNewTodo(pi);
            break;
        }
        case ED_NEW_TODO: {
            int pi = m_editPi;
            if (!trimmed.empty() && pi >= 0 && pi < (int)m_projects.size()) {
                m_projects[pi].todos.push_back({trimmed, false, now_iso()});
                saveAll();
                rebuildHits();
                m_cursorBlink = 0;
                requestRedraw();
            }
            endEdit(false);
            if (pi >= 0 && pi < (int)m_projects.size()) focusNewTodo(pi);
            break;
        }
        case ED_EDIT_TODO: {
            int pi = m_editPi, ti = m_editTi;
            if (!trimmed.empty() && pi >= 0 && pi < (int)m_projects.size() &&
                ti >= 0 && ti < (int)m_projects[pi].todos.size()) {
                m_projects[pi].todos[ti].text = trimmed;
                saveAll();
            }
            endEdit(false);
            break;
        }
        case ED_PREF_PREFIX: {
            if (trimmed.size() > 5) trimmed = trimmed.substr(0, 5);
            m_cfg.title_prefix = trimmed;
            saveAll();
            endEdit(false);
            closeSettings();
            break;
        }
        default: break;
    }
}
void App::addProjectCommit(const std::wstring& name) {
    m_projects.push_back({name, {}});
    m_addingProject = false;
    saveAll();
    int ni = (int)m_projects.size() - 1;
    endEdit(false);
    focusNewTodo(ni);
}
void App::focusNewTodo(int pi) {
    beginEdit(ED_NEW_TODO, pi, -1, L"");
    // 只滚动到新任务行刚好可见，而不是跳到整页最底端。
    float viewH = m_h - AppC::TITLE_H - AppC::BOT_H;
    float target = m_scroll;
    for (auto& hit : m_hits) {
        if (hit.type == H_NEWTODO && hit.pi == pi) {
            float top = hit.rc.top;
            float bot = hit.rc.bottom;
            if (bot > m_scroll + viewH - 8.0f) target = bot - viewH + 8.0f;
            else if (top < m_scroll + 8.0f) target = top - 8.0f;
            break;
        }
    }
    m_scrollTarget = target;
    clampScroll();
}
void App::completeTodo(int pi, int ti) {
    m_fades.push_back({pi, ti, 1.0f, 0.0f});
    requestRedraw();
}
void App::delProject(int pi) {
    if (pi < 0 || pi >= (int)m_projects.size()) return;
    m_projects.erase(m_projects.begin() + pi);
    saveAll();
    requestRedraw();
}
void App::openSettings() {
    m_settings = true;
    m_setScroll = 0; m_setScrollTarget = 0;
    requestRedraw();
}
void App::closeSettings() {
    m_settings = false;
    if (m_editMode == ED_PREF_PREFIX) endEdit(false);
    requestRedraw();
}
void App::openAbout() { m_about = true; m_aboutScroll = 0; m_aboutScrollTarget = 0; requestRedraw(); }
void App::closeAbout() { m_about = false; requestRedraw(); }
static std::wstring HtmlEsc(const std::wstring& s) {
    std::wstring o; o.reserve(s.size() + 8);
    for (wchar_t c : s) {
        switch (c) {
            case L'&': o += L"&amp;"; break;
            case L'<': o += L"&lt;"; break;
            case L'>': o += L"&gt;"; break;
            case L'"': o += L"&quot;"; break;
            case L'\'': o += L"&#39;"; break;
            default: o += c;
        }
    }
    return o;
}
void App::openHistory() {
    auto items = load_history();
    std::wstring h;
    std::wstring prefix = m_cfg.title_prefix.empty() ? L"\u4f60\u597d" : m_cfg.title_prefix;
    h += L"<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">";
    h += L"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    h += L"<title>" + HtmlEsc(prefix) + L"\uff0c\u4f60\u7684\u5386\u53f2\u4ee3\u529e\u4efb\u52a1</title>";
    h += L"<meta http-equiv=\"Cache-Control\" content=\"no-cache\"><meta http-equiv=\"Pragma\" content=\"no-cache\"><style>";
    h += L"body{font-family:'Microsoft YaHei',sans-serif;background:#f5f5f7;color:#1d1d1f;margin:0;padding:24px;}";
    h += L".wrap{max-width:760px;margin:0 auto;}h1{font-size:22px;margin:0;}";
    h += L".sum{color:#86868b;font-size:13px;margin:8px 0 18px;}";
    h += L".card{background:#fff;border-radius:12px;padding:16px 18px;margin-bottom:14px;box-shadow:0 1px 4px rgba(0,0,0,.06);}";
    h += L".proj{font-size:16px;font-weight:bold;margin:0 0 10px;}";
    h += L"table{width:100%;border-collapse:collapse;font-size:13px;}";
    h += L"th,td{text-align:left;padding:7px 20px;border-bottom:1px solid #e5e5e5;vertical-align:top;}";
    h += L"th{color:#86868b;font-weight:normal;font-size:12px;}";
    h += L".dur{color:#0066cc;white-space:nowrap;}";
    h += L".tm{white-space:nowrap;}";
    h += L".empty{color:#86868b;padding:40px 0;text-align:center;}";
    h += L"footer{color:#aaa;font-size:12px;text-align:center;margin-top:24px;}";
    h += L"</style></head><body><div class=\"wrap\">";
    h += L"<h1>" + HtmlEsc(prefix) + L"\uff0c\u4f60\u7684\u5386\u53f2\u4ee3\u529e\u4efb\u52a1</h1>";
    h += L"<div class=\"sum\">\u5171\u5b8c\u6210 " + std::to_wstring((int)items.size()) + L" \u6761\u4efb\u52a1</div>";
    if (items.empty()) {
        h += L"<div class=\"empty\">\u8fd8\u6ca1\u6709\u5b8c\u6210\u8fc7\u4efb\u52a1\uff0c\u70b9\u51fb\u5f85\u529e\u524d\u7684\u5706\u5708\u5b8c\u6210\u5373\u53ef\u8bb0\u5f55\u5230\u8fd9\u91cc\u3002</div>";
    } else {
        std::vector<std::pair<std::wstring, std::vector<HistoryItem>>> groups;
        for (auto& it : items) {
            bool found = false;
            for (auto& g : groups) if (g.first == it.project) { g.second.push_back(it); found = true; break; }
            if (!found) groups.push_back({it.project, {it}});
        }
        for (auto& g : groups) {
            h += L"<div class=\"card\"><div class=\"proj\">" + HtmlEsc(g.first) + L"</div><table>";
            h += L"<tr><th>\u4efb\u52a1</th><th>\u5efa\u7acb\u65f6\u95f4</th><th>\u5b8c\u6210\u65f6\u95f4</th><th>\u8017\u65f6</th></tr>";
            for (auto& it : g.second) {
                h += L"<tr><td>" + HtmlEsc(it.text) + L"</td>";
                h += L"<td class=\"tm\">" + HtmlEsc(iso_to_display(it.created)) + L"</td>";
                h += L"<td class=\"tm\">" + HtmlEsc(iso_to_display(it.completed)) + L"</td>";
                h += L"<td class=\"dur\">" + HtmlEsc(duration_text(it.created, it.completed)) + L"</td></tr>";
            }
            h += L"</table></div>";
        }
    }
    h += L"<footer>\u7531 ToDoWell \u751f\u6210 \u00b7 " + HtmlEsc(kCopyright) + L"</footer></div></body></html>";
    if (write_utf8_file(L"history.html", h)) {
        ShellExecuteW(m_hwnd, L"open", (exe_dir() + L"history.html").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        MessageBoxW(m_hwnd, L"\u65e0\u6cd5\u5199\u5165\u5386\u53f2\u9875\u9762\uff08history.html\uff09\u3002", L"ToDoWell", MB_OK | MB_ICONWARNING);
    }
}
float App::todoRowH(const std::wstring& text, float maxW) {
    if (text.empty()) return AppC::ROW_H;
    IDWriteTextLayout* lay = nullptr;
    float h = AppC::ROW_H;
    if (SUCCEEDED(g_gfx.dw->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                                             g_gfx.font(F_TODO), maxW, 1000.0f, &lay)) && lay) {
        DWRITE_TEXT_METRICS tm = {};
        if (SUCCEEDED(lay->GetMetrics(&tm)) && tm.height > 0) h = tm.height + 4.0f;
        lay->Release();
    }
    return h < AppC::ROW_H ? AppC::ROW_H : h;
}
float App::rowHForTodo(int pi, int ti, float maxW) {
    float h = todoRowH(m_projects[pi].todos[ti].text, maxW);
    if (m_editMode == ED_EDIT_TODO && m_editPi == pi && m_editTi == ti) {
        h = std::max(h, todoRowH(m_editText + m_compositionText, maxW));
    }
    return h;
}
float App::todoCircleY(const std::wstring& text, float rowTop, float maxW) {
    float y = rowTop + AppC::ROW_H * 0.5f;
    IDWriteTextLayout* lay = nullptr;
    if (SUCCEEDED(g_gfx.dw->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                                             g_gfx.font(F_TODO), maxW, 1000.0f, &lay)) && lay) {
        DWRITE_TEXT_METRICS tm = {};
        UINT32 lines = 0;
        if (SUCCEEDED(lay->GetMetrics(&tm)) &&
            SUCCEEDED(lay->GetLineMetrics(nullptr, 0, &lines)) && lines > 0) {
            float lineH = tm.height / (float)lines;
            float rh = todoRowH(text, maxW);
            float textTop = rowTop + (rh - tm.height) * 0.5f;
            y = textTop + lineH * 0.5f;
        }
        lay->Release();
    }
    return y;
}
void App::rebuildHits() {
    m_w = g_gfx.clientW();
    m_h = g_gfx.clientH();
    m_hits.clear();
    m_contentH = 0;
    m_editRectDip = {};

    float contentLeft = AppC::CONTENT_PAD;
    float contentW = m_w - 2 * AppC::CONTENT_PAD;
    if (contentW < 10) contentW = 10;
    float y = AppC::CONTENT_TOP;

    auto screenY = [&](float cy) { return AppC::TITLE_H + cy - m_scroll; };

    for (size_t pi = 0; pi < m_projects.size(); ++pi) {
        auto& proj = m_projects[pi];
        float cardTop = y;
        float hy = y + AppC::CARD_TOP;
        float badgeX = contentLeft + AppC::CARD_INNER;
        std::wstring num = std::to_wstring((int)pi + 1);
        float tw = g_gfx.measureTextW(num, F_PROJ_NUM);
        float badgeW = std::max(AppC::BADGE_MIN_W, tw + 12.0f);
        float nameX = badgeX + badgeW + 6.0f;
        float delW = 22.0f;
        float nameRight = contentLeft + contentW - AppC::CARD_INNER - delW;
        m_hits.push_back({D2D1::RectF(nameX, hy, nameRight, hy + AppC::BADGE_H), H_PROJ_NAME, (int)pi, -1});
        float delX = contentLeft + contentW - AppC::CARD_INNER - delW;
        m_hits.push_back({D2D1::RectF(delX, hy, delX + delW, hy + AppC::BADGE_H), H_PROJ_DEL, (int)pi, -1});
        if (m_editMode == ED_EDIT_PROJECT && m_editPi == (int)pi) {
            m_editRectDip = D2D1::RectF(nameX, hy - 2, nameRight, hy + AppC::BADGE_H + 2);
        }
        float headerH = AppC::BADGE_H;
        y = hy + headerH + 6.0f;
        float cxRow = contentLeft + AppC::CARD_INNER + AppC::CIRCLE_R + 2.0f;
        float textXRow = cxRow + AppC::CIRCLE_R + 8.0f;
        float textRightRow = contentLeft + contentW - AppC::CARD_INNER;

        for (size_t ti = 0; ti < proj.todos.size(); ++ti) {
            bool fading = false;
            for (auto& f : m_fades) if (f.pi == (int)pi && f.ti == (int)ti) { fading = true; break; }
            float rowTop = y;
            float rh = rowHForTodo((int)pi, (int)ti, textRightRow - textXRow);
            if (fading) { y = rowTop + rh; continue; }
            m_hits.push_back({D2D1::RectF(cxRow - AppC::CIRCLE_R - 4, rowTop, cxRow + AppC::CIRCLE_R + 4, rowTop + rh), H_TODO_CIRCLE, (int)pi, (int)ti});
            m_hits.push_back({D2D1::RectF(textXRow, rowTop, textRightRow, rowTop + rh), H_TODO_TEXT, (int)pi, (int)ti});
            if (m_editMode == ED_EDIT_TODO && m_editPi == (int)pi && m_editTi == (int)ti) {
                m_editRectDip = D2D1::RectF(textXRow, rowTop, textRightRow, rowTop + rh);
            }
            y = rowTop + rh;
        }
        float ntTop = y;
        float ntH = AppC::ROW_H;
        if (m_editMode == ED_NEW_TODO && m_editPi == (int)pi) {
            ntH = todoRowH(m_editText + m_compositionText, textRightRow - textXRow);
        }
        m_hits.push_back({D2D1::RectF(contentLeft + AppC::CARD_INNER, ntTop, textRightRow, ntTop + ntH), H_NEWTODO, (int)pi, -1});
        if (m_editMode == ED_NEW_TODO && m_editPi == (int)pi) {
            m_editRectDip = D2D1::RectF(textXRow, ntTop, textRightRow, ntTop + ntH);
        }
        y = ntTop + ntH + 4.0f;
        float cardBottom = y + 6.0f;
        m_contentH = std::max(m_contentH, cardBottom + AppC::CARD_GAP);
        (void)screenY;
        y = cardBottom;
        y += AppC::CARD_GAP;
    }

    if (m_addingProject) {
        float cardTop2 = y;
        float hy2 = y + AppC::CARD_TOP;
        float nameX2 = contentLeft + AppC::CARD_INNER + 8.0f;
        float nameRight2 = contentLeft + contentW - AppC::CARD_INNER;
        if (m_editMode == ED_ADD_PROJECT) {
            m_editRectDip = D2D1::RectF(nameX2, hy2 - 2, nameRight2, hy2 + AppC::BADGE_H + 2);
        }
        y = hy2 + AppC::BADGE_H + 4.0f;
        m_contentH = std::max(m_contentH, y + AppC::CARD_GAP);
        (void)cardTop2;
    }
    (void)screenY;
    clampScroll();
}
D2D1_COLOR_F App::lerpColor(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) const {
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

// Measure the real ink bounding box of a symbol glyph (via glyph run
// analysis) and return the offset from the draw-rect center to the ink
// center, in DIPs. Rotating around this point keeps the icon spinning in
// place instead of wobbling around the rect center.
static D2D1_POINT_2F IconInkCenterOffset(Gfx& g, FontId fid, const wchar_t* s) {
    D2D1_POINT_2F off = {0, 0};
    IDWriteTextFormat* f = g.font(fid);
    IDWriteFontCollection* coll = nullptr;
    if (FAILED(f->GetFontCollection(&coll)) || !coll) return off;
    wchar_t family[128] = {};
    f->GetFontFamilyName(family, 128);
    UINT32 famIdx = 0; BOOL exists = FALSE;
    if (FAILED(coll->FindFamilyName(family, &famIdx, &exists)) || !exists) { coll->Release(); return off; }
    IDWriteFontFamily* fam = nullptr;
    if (FAILED(coll->GetFontFamily(famIdx, &fam)) || !fam) { coll->Release(); return off; }
    IDWriteFont* font = nullptr;
    if (FAILED(fam->GetFirstMatchingFont(f->GetFontWeight(), f->GetFontStretch(),
                                         f->GetFontStyle(), &font)) || !font) {
        fam->Release(); coll->Release(); return off;
    }
    IDWriteFontFace* face = nullptr;
    if (SUCCEEDED(font->CreateFontFace(&face)) && face) {
        DWRITE_FONT_METRICS fm = {};
        face->GetMetrics(&fm);
        UINT16 gi = 0;
        UINT32 cp = (UINT32)s[0];
        face->GetGlyphIndices(&cp, 1, &gi);
        DWRITE_GLYPH_METRICS gm = {};
        face->GetDesignGlyphMetrics(&gi, 1, &gm, FALSE);
        float em = f->GetFontSize();
        float adv = gm.advanceWidth * em / (float)fm.designUnitsPerEm;
        DWRITE_GLYPH_RUN run = {};
        run.fontFace = face;
        run.fontEmSize = em;
        run.glyphCount = 1;
        run.glyphIndices = &gi;
        run.glyphAdvances = &adv;
        IDWriteGlyphRunAnalysis* ana = nullptr;
        HRESULT hr = g.dw->CreateGlyphRunAnalysis(&run, 1.0f, nullptr,
                                                  DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL,
                                                  DWRITE_MEASURING_MODE_NATURAL,
                                                  0.0f, 0.0f, &ana);
        if (SUCCEEDED(hr) && ana) {
            const DWRITE_TEXTURE_TYPE types[3] = {
                DWRITE_TEXTURE_ALIASED_1x1,
                DWRITE_TEXTURE_CLEARTYPE_3x1 };
            // Bounds are returned in device pixels (the texture type only
            // affects the alpha data layout, not the coordinate space).
            const float sx[2] = {1.0f, 1.0f};
            const float sy[2] = {1.0f, 1.0f};
            for (int i = 0; i < 2; ++i) {
                RECT bnd = {};
                if (SUCCEEDED(ana->GetAlphaTextureBounds(types[i], &bnd)) &&
                    bnd.right > bnd.left && bnd.bottom > bnd.top) {
                    float inkCx = (((float)bnd.left + (float)bnd.right) * 0.5f) / sx[i];
                    float inkCy = (((float)bnd.top + (float)bnd.bottom) * 0.5f) / sy[i];
                    float ascent = em * (float)fm.ascent / (float)fm.designUnitsPerEm;
                    // Text metrics: width/height of the centered single glyph.
                    IDWriteTextLayout* lay = nullptr;
                    float textW = 0, lineH = 0;
                    if (SUCCEEDED(g.dw->CreateTextLayout(s, 1, f, 100.0f, 100.0f, &lay)) && lay) {
                        DWRITE_TEXT_METRICS tm = {};
                        if (SUCCEEDED(lay->GetMetrics(&tm))) { textW = tm.width; lineH = tm.height; }
                        lay->Release();
                    }
                    if (lineH <= 0)
                        lineH = em * (float)(fm.ascent + fm.descent + fm.lineGap) / (float)fm.designUnitsPerEm;
                    // For centered text the baseline sits at rectCenter + (ascent - lineH/2),
                    // and the pen origin at rectCenter - textW/2. The analysis
                    // ran with pixelsPerDip=1, so its pixel bounds are DIPs.
                    off.x = inkCx - textW * 0.5f;
                    off.y = inkCy + ascent - lineH * 0.5f;
                    break;
                }
            }
            ana->Release();
        }
        face->Release();
    }
    font->Release(); fam->Release(); coll->Release();
    return off;
}

void App::render() {
    Gfx& g = g_gfx;
    float W = g.clientW(), H = g.clientH();
    g.rt->BeginDraw();
    // 用不透明的页面背景清屏：Win7 上透明黑清屏会在栏边界露出黑线。
    g.rt->Clear(D2D1::ColorF(C::PAGE.r, C::PAGE.g, C::PAGE.b, 1.0f));
    g.fillRect(D2D1::RectF(0, 0, W, H), C::PAGE);

    // title bar
    g.fillRect(D2D1::RectF(0, 0, W, AppC::TITLE_H), C::TITLE_BG);
    float tx = 12.0f;
    if (!m_cfg.title_prefix.empty()) {
        g.drawText(m_cfg.title_prefix, D2D1::RectF(tx, 0, tx + 200, AppC::TITLE_H), F_TITLE_PRE, C::TITLE_FG);
        tx += g.measureTextW(m_cfg.title_prefix, F_TITLE_PRE);
        tx += g.measureTextW(L" ", F_TITLE_PRE);
    }
    g.drawText(L"gogogo!!!", D2D1::RectF(tx, 0, tx + 200, AppC::TITLE_H), F_TITLE_BIG, C::TITLE_FG);

    // title buttons
    float closeW = 32.0f, snapW = 32.0f, gap = 8.0f, rightPad = 12.0f, btnH = 24.0f;
    float btnY1 = (AppC::TITLE_H - btnH) / 2.0f, btnY2 = btnY1 + btnH;
    float closeX = W - rightPad - closeW, snapX = closeX - gap - snapW;
    D2D1_COLOR_F snapCol = lerpColor(C::TITLE_FG, C::ACCENT, m_snapT);
    g.drawText(L"\u2192", D2D1::RectF(snapX, btnY1, snapX + snapW, btnY2), F_SYM_TITLE, snapCol,
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    D2D1_COLOR_F closeCol = lerpColor(C::DEL_BTN, C::DEL_HOVER, m_closeT);
    g.drawText(L"\u2715", D2D1::RectF(closeX, btnY1, closeX + closeW, btnY2), F_SYM_TITLE, closeCol,
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // content area
    float cTop = AppC::TITLE_H;
    float cBot = H - AppC::BOT_H;
    float contentLeft = AppC::CONTENT_PAD;
    float contentW = W - 2 * AppC::CONTENT_PAD;
    g.rt->PushAxisAlignedClip(D2D1::RectF(0, cTop, W, cBot), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    g.fillRect(D2D1::RectF(0, cTop, W, cBot), C::PAGE);

    float scrollOff = m_scroll;
    float y = AppC::CONTENT_TOP;

    for (size_t pi = 0; pi < m_projects.size(); ++pi) {
        auto& proj = m_projects[pi];
        float cxRow = contentLeft + AppC::CARD_INNER + AppC::CIRCLE_R + 2.0f;
        float textXRow = cxRow + AppC::CIRCLE_R + 8.0f;
        float textRightRow = contentLeft + contentW - AppC::CARD_INNER;
        float rowsH = 0;
        for (size_t ti = 0; ti < proj.todos.size(); ++ti)
            rowsH += rowHForTodo((int)pi, (int)ti, textRightRow - textXRow);
        float ntH = AppC::ROW_H;
        if (m_editMode == ED_NEW_TODO && m_editPi == (int)pi)
            ntH = todoRowH(m_editText + m_compositionText, textRightRow - textXRow);
        // Card white background
        float cardContentBot = y + AppC::CARD_TOP + AppC::BADGE_H + 6.0f
                              + rowsH + ntH + 4.0f + 6.0f;
        float scrTop = cTop + y - scrollOff;
        float scrBot = cTop + cardContentBot - scrollOff;
        g.fillRoundedRect(D2D1::RectF(contentLeft, scrTop, contentLeft + contentW, scrBot), 8.0f, C::WHITE);
        float hy = y + AppC::CARD_TOP;
        float screenHy = cTop + hy - scrollOff;
        float badgeX = contentLeft + AppC::CARD_INNER;
        std::wstring num = std::to_wstring((int)pi + 1);
        float tw = g.measureTextW(num, F_PROJ_NUM);
        float badgeW = std::max(AppC::BADGE_MIN_W, tw + 12.0f);
        DWORD col = C::PROJ_COLORS[pi % 8];
        g.fillRoundedRect(D2D1::RectF(badgeX, screenHy, badgeX + badgeW, screenHy + AppC::BADGE_H), 4.0f, Hex(col));
        g.drawText(num, D2D1::RectF(badgeX, screenHy, badgeX + badgeW, screenHy + AppC::BADGE_H), F_PROJ_NUM, C::WHITE,
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        float nameX = badgeX + badgeW + 6.0f;
        float delW = 22.0f;
        float nameRight = contentLeft + contentW - AppC::CARD_INNER - delW;
        if (m_editMode == ED_EDIT_PROJECT && m_editPi == (int)pi) {
            std::wstring dtext = m_editText + m_compositionText;
            // Full-width underline, matching the todo row editing style.
            g.drawLine(nameX, screenHy + AppC::BADGE_H - 1, nameRight, screenHy + AppC::BADGE_H - 1, C::ACCENT, 1.5f);
            g.drawText(dtext, D2D1::RectF(nameX, screenHy, nameRight, screenHy + AppC::BADGE_H), F_PROJ_NAME, C::TEXT);
            drawEditCaret(g, F_PROJ_NAME, dtext, nameX, screenHy, nameRight - nameX, AppC::BADGE_H);
        } else {
            g.drawText(proj.name, D2D1::RectF(nameX, screenHy, nameRight, screenHy + AppC::BADGE_H), F_PROJ_NAME, C::TEXT);
        }
        float delX = contentLeft + contentW - AppC::CARD_INNER - delW;
        D2D1_COLOR_F delCol = lerpColor(C::DEL_BTN, C::DEL_HOVER, m_projDelT);
        D2D1_RECT_F delRc = D2D1::RectF(delX, screenHy, delX + delW, screenHy + AppC::BADGE_H);
        D2D1_POINT_2F delOff = IconInkCenterOffset(g, F_SYM_TITLE, L"\u2715");
        D2D1_POINT_2F delC = { delRc.left + (delRc.right - delRc.left) * 0.5f + delOff.x,
                               delRc.top + (delRc.bottom - delRc.top) * 0.5f + delOff.y };
        // Only the hovered delete button spins; other cards' buttons stay still.
        float delSpin = (m_hoverProjDel == (int)pi) ? m_projDelSpin : 0.0f;
        g.rt->SetTransform(D2D1::Matrix3x2F::Rotation(delSpin * 360.0f, delC));
        g.drawText(L"\u2715", delRc, F_SYM_TITLE, delCol,
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        g.rt->SetTransform(D2D1::Matrix3x2F::Identity());
        y = hy + AppC::BADGE_H + 6.0f;

        for (size_t ti = 0; ti < proj.todos.size(); ++ti) {
            bool fading = false;
            float fa = 1.0f, fo = 0.0f;
            for (auto& f : m_fades) if (f.pi == (int)pi && f.ti == (int)ti) { fading = true; fa = f.alpha; fo = f.off; break; }
            float rh = rowHForTodo((int)pi, (int)ti, textRightRow - textXRow);
            if (fading) {
                float cy = todoCircleY(proj.todos[ti].text, y, textRightRow - textXRow);
                float sy = cTop + y - scrollOff + fo;
                D2D1_COLOR_F cc = D2D1::ColorF(C::ACCENT.r, C::ACCENT.g, C::ACCENT.b, fa);
                g.drawEllipse(cxRow, cy + (cTop - scrollOff) + fo, AppC::CIRCLE_R, AppC::CIRCLE_R, cc, 1.8f);
                g.rt->SetTransform(D2D1::Matrix3x2F::Identity());
                D2D1_COLOR_F tc = D2D1::ColorF(C::TEXT.r, C::TEXT.g, C::TEXT.b, fa);
                g.drawText(proj.todos[ti].text, D2D1::RectF(textXRow, cTop + y - scrollOff + fo, textRightRow, cTop + y - scrollOff + fo + rh), F_TODO, tc,
                           DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                (void)fa; (void)fo;
                y += rh;
                continue;
            }
            float rowTop = y;
            std::wstring dispText = proj.todos[ti].text;
            if (m_editMode == ED_EDIT_TODO && m_editPi == (int)pi && m_editTi == (int)ti)
                dispText = m_editText + m_compositionText;
            float cy = todoCircleY(dispText, rowTop, textRightRow - textXRow);
            float sy = cTop + rowTop - scrollOff;
            bool hov = (m_hovCircPi == (int)pi && m_hovCircTi == (int)ti);
            D2D1_COLOR_F cc = hov ? lerpColor(C::CIRCLE, C::ACCENT, m_circT) : C::CIRCLE;
            g.drawEllipse(cxRow, cy + (cTop - scrollOff), AppC::CIRCLE_R, AppC::CIRCLE_R, cc, 1.8f);
            if (!(m_editMode == ED_EDIT_TODO && m_editPi == (int)pi && m_editTi == (int)ti)) {
                g.drawText(proj.todos[ti].text, D2D1::RectF(textXRow, sy, textRightRow, sy + rh), F_TODO, C::TEXT,
                           DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            } else {
                // Editing state: full-width accent underline, matching the
                // project title and new-task rows so the edit affordance is clear.
                g.drawLine(textXRow, sy + rh - 2, textRightRow, sy + rh - 2, C::ACCENT, 1.5f);
                std::wstring dtext = m_editText + m_compositionText;
                g.drawText(dtext, D2D1::RectF(textXRow, sy, textRightRow, sy + rh), F_TODO, C::TEXT,
                           DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                drawEditCaret(g, F_TODO, dtext, textXRow, sy, textRightRow - textXRow, rh);
            }
            y += rh;
        }
        {
            float ntTop = y;
            float cy = ntTop + AppC::ROW_H / 2.0f;
            float sy = cTop + ntTop - scrollOff;
            g.drawEllipse(cxRow, cy + (cTop - scrollOff), AppC::CIRCLE_R, AppC::CIRCLE_R, C::CIRCLE, 1.5f);
            if (m_editMode == ED_NEW_TODO && m_editPi == (int)pi) {
                g.drawLine(textXRow, sy + ntH - 2, textRightRow, sy + ntH - 2, C::ACCENT, 1.5f);
                std::wstring dtext = m_editText + m_compositionText;
                g.drawText(dtext, D2D1::RectF(textXRow, sy, textRightRow, sy + ntH), F_TODO, C::TEXT,
                           DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                drawEditCaret(g, F_TODO, dtext, textXRow, sy, textRightRow - textXRow, ntH);
            }
        }
        y += ntH + 4.0f + 6.0f + AppC::CARD_GAP;

    }
    m_contentH = std::max(m_contentH, y + AppC::CARD_GAP);
    if (m_addingProject) {
        float hy = y + AppC::CARD_TOP;
        float screenTop = cTop + y - scrollOff;
        float screenBot = screenTop + AppC::BADGE_H + 14.0f;
        D2D1_RECT_F cardRc = D2D1::RectF(contentLeft, screenTop, contentLeft + contentW, screenBot);
        g.fillRoundedRect(cardRc, 8.0f, C::WHITE);
        g.fillRect(D2D1::RectF(contentLeft, screenTop, contentLeft + 4.0f, screenBot), C::ACCENT);
        if (m_editMode != ED_ADD_PROJECT) {
            g.drawText(L"\u65b0\u9879\u76ee\u540d\u79f0", D2D1::RectF(contentLeft + AppC::CARD_INNER + 8.0f, screenTop, contentLeft + contentW - AppC::CARD_INNER, screenBot), F_PROJ_NAME, C::MUTED);
        } else {
            float ex = contentLeft + AppC::CARD_INNER + 8.0f;
            float ew = contentW - AppC::CARD_INNER * 2 - 8.0f;
            std::wstring dtext = m_editText + m_compositionText;
            // Keep the input text vertically centered in the card, exactly
            // where the "新项目名称" placeholder is drawn.
            g.drawLine(ex, screenTop + 30, ex + ew, screenTop + 30, C::ACCENT, 1.5f);
            g.drawText(dtext, D2D1::RectF(ex, screenTop, ex + ew, screenBot), F_PROJ_NAME, C::TEXT);
            drawEditCaret(g, F_PROJ_NAME, dtext, ex, screenTop, ew, 30.0f);
        }
    }
    g.rt->PopAxisAlignedClip();

    // bottom bar
    g.fillRect(D2D1::RectF(0, H - AppC::BOT_H, W, H), C::PAGE);
    // Rotate each icon around its own ink center (measured via glyph run
    // analysis), not the button rect center.
    D2D1_COLOR_F addCol = lerpColor(C::TEXT, C::ACCENT, m_addT);
    D2D1_RECT_F addRc = D2D1::RectF(8, H - AppC::BOT_H, 44, H);
    D2D1_POINT_2F addOff = IconInkCenterOffset(g, F_SYM_BOTTOM, L"+");
    D2D1_POINT_2F addC = { addRc.left + (addRc.right - addRc.left) * 0.5f + addOff.x,
                           addRc.top + (addRc.bottom - addRc.top) * 0.5f + addOff.y };
    g.rt->SetTransform(D2D1::Matrix3x2F::Rotation(m_addSpin * 360.0f, addC));
    g.drawText(L"+", addRc, F_SYM_BOTTOM, addCol,
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g.rt->SetTransform(D2D1::Matrix3x2F::Identity());
    D2D1_COLOR_F gearCol = lerpColor(C::TEXT, C::ACCENT, m_gearT);
    D2D1_RECT_F gearRc = D2D1::RectF(W - 44, H - AppC::BOT_H, W - 8, H);
    D2D1_POINT_2F gearOff = IconInkCenterOffset(g, F_SYM_BOTTOM, L"\u2699");
    D2D1_POINT_2F gearC = { gearRc.left + (gearRc.right - gearRc.left) * 0.5f + gearOff.x,
                            gearRc.top + (gearRc.bottom - gearRc.top) * 0.5f + gearOff.y };
    g.rt->SetTransform(D2D1::Matrix3x2F::Rotation(m_gearSpin * 360.0f, gearC));
    g.drawText(L"\u2699", gearRc, F_SYM_BOTTOM, gearCol,
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g.rt->SetTransform(D2D1::Matrix3x2F::Identity());
    // overlays
    if (m_overlayAlpha > 0.01f || m_settings || m_about) {
        float oa = m_overlayAlpha;
        g.fillRect(D2D1::RectF(0, 0, W, H), D2D1::ColorF(0, 0, 0, 0.35f * oa));
    }
    if (m_settings && m_overlayAlpha > 0.01f && !m_about) {
        float dlgW = 220.0f, dlgH = 230.0f;
        float dlgX = (W - dlgW) / 2.0f, dlgY = (H - dlgH) / 2.0f;
        float oa = m_overlayAlpha;
        g.fillRoundedRect(D2D1::RectF(dlgX, dlgY, dlgX + dlgW, dlgY + dlgH), 10.0f, D2D1::ColorF(C::DIALOG_BG.r, C::DIALOG_BG.g, C::DIALOG_BG.b, oa));
        float setScrollTop = dlgY + 12.0f;
        float setScrollBot = dlgY + dlgH - 40.0f;
        float sy = -m_setScroll;
        g.rt->PushAxisAlignedClip(D2D1::RectF(dlgX, setScrollTop, dlgX + dlgW, setScrollBot), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float cy = dlgY + 14.0f + sy, cb = 18.0f;
        float cbX = dlgX + 16.0f;
        g.strokeRoundedRect(D2D1::RectF(cbX, cy, cbX + cb, cy + cb), 4.0f, D2D1::ColorF(C::INPUT_BD.r, C::INPUT_BD.g, C::INPUT_BD.b, oa), 1.2f);
        if (m_cfg.auto_start) {
            g.fillRoundedRect(D2D1::RectF(cbX, cy, cbX + cb, cy + cb), 4.0f, D2D1::ColorF(C::ACCENT.r, C::ACCENT.g, C::ACCENT.b, oa));
            g.drawText(L"\u2713", D2D1::RectF(cbX, cy - 2, cbX + cb, cy + cb), F_SYM_CHECK, D2D1::ColorF(1, 1, 1, oa),
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        g.drawText(L"\u5f00\u673a\u81ea\u52a8\u542f\u52a8", D2D1::RectF(cbX + cb + 8, cy, dlgX + dlgW - 24, cy + cb), F_SETTINGS, D2D1::ColorF(C::DIALOG_TX.r, C::DIALOG_TX.g, C::DIALOG_TX.b, oa),
                   DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        float ly = cy + cb + 8.0f;
        g.drawText(L"\u6807\u9898\u524d\u7f00\uff08\u6700\u591a5\u4e2a\u6c49\u5b57\uff09\uff1a", D2D1::RectF(dlgX + 16, ly, dlgX + dlgW - 16, ly + 16), F_HINT, D2D1::ColorF(C::DIALOG_MM.r, C::DIALOG_MM.g, C::DIALOG_MM.b, oa));
        float fy = ly + 18.0f;
        g.fillRoundedRect(D2D1::RectF(dlgX + 16, fy, dlgX + dlgW - 16, fy + 26), 4.0f, D2D1::ColorF(1, 1, 1, oa));
        g.strokeRoundedRect(D2D1::RectF(dlgX + 16, fy, dlgX + dlgW - 16, fy + 26), 4.0f, D2D1::ColorF(C::INPUT_BD.r, C::INPUT_BD.g, C::INPUT_BD.b, oa), 1.0f);
        if (!(m_editMode == ED_PREF_PREFIX)) {
            g.drawText(m_cfg.title_prefix.empty() ? L"\u4f60\u597d" : m_cfg.title_prefix,
                       D2D1::RectF(dlgX + 22, fy, dlgX + dlgW - 22, fy + 26), F_SETTINGS, D2D1::ColorF(C::DIALOG_TX.r, C::DIALOG_TX.g, C::DIALOG_TX.b, oa),
                       DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        } else {
            std::wstring dtext = m_editText + m_compositionText;
            g.drawText(dtext, D2D1::RectF(dlgX + 22, fy, dlgX + dlgW - 22, fy + 26), F_SETTINGS, D2D1::ColorF(C::DIALOG_TX.r, C::DIALOG_TX.g, C::DIALOG_TX.b, oa),
                       DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            float tw = g.measureTextW(m_editText, F_SETTINGS);
            float cw = g.measureTextW(m_compositionText, F_SETTINGS);
            if (((int)(m_cursorBlink * 2) % 2) == 0)
                g.drawLine(dlgX + 22 + tw + cw + 1, fy + 3, dlgX + 22 + tw + cw + 1, fy + 23, D2D1::ColorF(C::ACCENT.r, C::ACCENT.g, C::ACCENT.b, oa), 1.5f);
        }
        float ay = fy + 26 + 10.0f;
        float say = ay;
        g.drawText(L"\u5f52\u4f4d\u52a8\u753b\uff1a", D2D1::RectF(dlgX + 16, say, dlgX + dlgW - 16, say + 16), F_HINT, D2D1::ColorF(C::DIALOG_MM.r, C::DIALOG_MM.g, C::DIALOG_MM.b, oa));
        say += 18.0f;
        const wchar_t* animNames[] = { L"\u65e0\u52a8\u753b", L"\u52a8\u753b1", L"\u52a8\u753b2", L"\u52a8\u753b3", L"\u52a8\u753b4" };
        float optW = 52, optH = 22, optGap = 8;
        int perRow = 3;
        for (int i = 0; i < 5; ++i) {
            int row = i / perRow, col = i % perRow;
            float ox = dlgX + 16 + col * (optW + optGap);
            float oy = say + row * (optH + optGap);
            bool sel = (m_cfg.snap_anim == i);
            if (sel) g.fillRoundedRect(D2D1::RectF(ox, oy, ox + optW, oy + optH), 4.0f, D2D1::ColorF(C::ACCENT.r, C::ACCENT.g, C::ACCENT.b, oa));
            else g.strokeRoundedRect(D2D1::RectF(ox, oy, ox + optW, oy + optH), 4.0f, D2D1::ColorF(C::INPUT_BD.r, C::INPUT_BD.g, C::INPUT_BD.b, oa), 1.0f);
            D2D1_COLOR_F tx = sel ? D2D1::ColorF(1, 1, 1, oa) : D2D1::ColorF(C::DIALOG_TX.r, C::DIALOG_TX.g, C::DIALOG_TX.b, oa);
            g.drawText(animNames[i], D2D1::RectF(ox, oy, ox + optW, oy + optH), F_HINT, tx,
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        say += 2 * optH + optGap + 6.0f;
        D2D1_COLOR_F aboutCol = D2D1::ColorF(C::ACCENT.r, C::ACCENT.g, C::ACCENT.b, oa);
        float py = say;
        g.drawText(L"\u9ed8\u8ba4\u7a97\u53e3\u4f4d\u7f6e\uff1a", D2D1::RectF(dlgX + 16, py, dlgX + dlgW - 16, py + 16), F_HINT, D2D1::ColorF(C::DIALOG_MM.r, C::DIALOG_MM.g, C::DIALOG_MM.b, oa));
        const wchar_t* posNames[4] = { L"\u53f3\u4e0b\u89d2", L"\u5de6\u4e0a\u89d2", L"\u5de6\u4e0b\u89d2", L"\u53f3\u4e0a\u89d2" };
        float posW = 92, posH = 22, posGap = 8;
        for (int i = 0; i < 4; ++i) {
            int row = i / 2, col = i % 2;
            float ox = dlgX + 16 + col * (posW + posGap);
            float oy = py + 18 + row * (posH + posGap);
            bool sel = (m_cfg.default_pos == i);
            if (sel) g.fillRoundedRect(D2D1::RectF(ox, oy, ox + posW, oy + posH), 4.0f, D2D1::ColorF(C::ACCENT.r, C::ACCENT.g, C::ACCENT.b, oa));
            else g.strokeRoundedRect(D2D1::RectF(ox, oy, ox + posW, oy + posH), 4.0f, D2D1::ColorF(C::INPUT_BD.r, C::INPUT_BD.g, C::INPUT_BD.b, oa), 1.0f);
            D2D1_COLOR_F ptx = sel ? D2D1::ColorF(1, 1, 1, oa) : D2D1::ColorF(C::DIALOG_TX.r, C::DIALOG_TX.g, C::DIALOG_TX.b, oa);
            g.drawText(posNames[i], D2D1::RectF(ox, oy, ox + posW, oy + posH), F_HINT, ptx,
                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        float hy = py + 18 + 2 * posH + posGap + 6.0f;
        g.drawText(L"\u5386\u53f2\u4ee3\u529e\u4efb\u52a1", D2D1::RectF(dlgX + 16, hy, dlgX + dlgW - 16, hy + 18), F_HINT, aboutCol);
        float aboutY = hy + 22.0f;
        g.drawText(L"\u5173\u4e8e ToDoWell", D2D1::RectF(dlgX + 16, aboutY, dlgX + dlgW - 16, aboutY + 18), F_HINT, aboutCol);
        // Win7 输入法提示：内部版（5014）/ 公开版按版权署名自动区分
        const bool internalBuild = std::wstring(kCopyright).find(L"5014") != std::wstring::npos;
        const wchar_t* win7Note = internalBuild
            ? L"\u6ce8\u610f\uff1a\u5982\u679c\u5728Win7\u7cfb\u7edf\u8f93\u5165\u6587\u5b57\u65f6\u65e0\u6cd5\u663e\u793a\u6587\u672c\u5019\u9009\u6846\uff0c\u8bf7\u8054\u7cfb5014\u83b7\u53d6\u6700\u65b0\u7248\u641c\u72d7\u8f93\u5165\u6cd5\u5b89\u88c5\u5305\uff0c\u5b89\u88c5\u540e\u5373\u53ef\u89e3\u51b3\uff08\u65e0\u9700\u7ba1\u7406\u5458\u6743\u9650\uff09\u3002"
            : L"\u6ce8\u610f\uff1a\u5982\u679c\u5728Win7\u7cfb\u7edf\u8f93\u5165\u6587\u5b57\u65f6\u65e0\u6cd5\u663e\u793a\u6587\u672c\u5019\u9009\u6846\uff0c\u8bf7\u5b89\u88c5\u6700\u65b0\u7248\u641c\u72d7\u8f93\u5165\u6cd5\u5b89\u88c5\u5305\uff0c\u5b89\u88c5\u540e\u5373\u53ef\u89e3\u51b3\u3002Win10/11\u65e0\u6b64\u95ee\u9898\u3002";
        float noteContentW = dlgW - 32.0f;
        float noteTextH = 40.0f;
        IDWriteTextLayout* noteLay = nullptr;
        if (SUCCEEDED(g.dw->CreateTextLayout(win7Note, (UINT32)wcslen(win7Note), g.font(F_HINT), noteContentW, 200.0f, &noteLay)) && noteLay) {
            DWRITE_TEXT_METRICS ntm = {};
            if (SUCCEEDED(noteLay->GetMetrics(&ntm)) && ntm.height > 0) noteTextH = ntm.height;
            SafeRelease(noteLay);
        }
        float noteY = aboutY + 22.0f;
        float noteH = noteTextH + 8.0f;
        g.fillRoundedRect(D2D1::RectF(dlgX + 8, noteY, dlgX + dlgW - 8, noteY + noteH), 6.0f,
                          D2D1::ColorF(C::TITLE_BG.r, C::TITLE_BG.g, C::TITLE_BG.b, oa));
        g.drawText(win7Note, D2D1::RectF(dlgX + 12, noteY + 4, dlgX + dlgW - 12, noteY + noteH - 4), F_HINT,
                   D2D1::ColorF(C::TITLE_FG.r, C::TITLE_FG.g, C::TITLE_FG.b, oa),
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        g.rt->PopAxisAlignedClip();
        g.drawText(L"\u7248\u672c 2.5.10", D2D1::RectF(dlgX + 16, dlgY + dlgH - 34, dlgX + dlgW - 16, dlgY + dlgH - 20), F_FOOTER, D2D1::ColorF(C::DIALOG_FT.r, C::DIALOG_FT.g, C::DIALOG_FT.b, oa));
        g.drawText(kCopyright, D2D1::RectF(dlgX + 16, dlgY + dlgH - 20, dlgX + dlgW - 16, dlgY + dlgH - 6), F_FOOTER, D2D1::ColorF(C::DIALOG_FT.r, C::DIALOG_FT.g, C::DIALOG_FT.b, oa));
        if (m_editMode == ED_PREF_PREFIX) m_editRectDip = D2D1::RectF(dlgX + 16, fy, dlgX + dlgW - 16, fy + 26);
        float posBottom = noteY + noteH;
        // Content height must be independent of the current scroll offset;
        // posBottom already moves with sy, so subtract it back out.
        m_setContentH = (posBottom + 10.0f - sy) - dlgY + 10.0f;
    }
    if (m_about && m_aboutAlpha > 0.01f) {
        float dlgW = 220.0f, dlgH = 230.0f;
        float dlgX = (W - dlgW) / 2.0f, dlgY = (H - dlgH) / 2.0f;
        float oa = m_aboutAlpha;
        g.fillRoundedRect(D2D1::RectF(dlgX, dlgY, dlgX + dlgW, dlgY + dlgH), 10.0f, D2D1::ColorF(C::DIALOG_BG.r, C::DIALOG_BG.g, C::DIALOG_BG.b, oa));
        float abScrollTop = dlgY + 12.0f;
        float abScrollBot = dlgY + dlgH - 40.0f;
        float sy = -m_aboutScroll;
        g.rt->PushAxisAlignedClip(D2D1::RectF(dlgX, abScrollTop, dlgX + dlgW, abScrollBot), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float ty = dlgY + 14.0f + sy;
        g.drawText(L"ToDoWell", D2D1::RectF(dlgX + 16, ty, dlgX + dlgW - 16, ty + 28), F_ABOUT_TITLE, D2D1::ColorF(C::DIALOG_TX.r, C::DIALOG_TX.g, C::DIALOG_TX.b, oa), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        ty += 34.0f;
        const wchar_t* feats[] = {
            L"\u2022 项目分类管理：新建、双击改名、一键删除，8 色编号徽标",
            L"\u2022 待办任务：点击圆圈完成（淡出动画）、单击编辑、回车连续新增",
            L"\u2022 中文输入法：拼音组合与候选框跟随光标，Win7 / Win11 均正常",
            L"\u2022 自绘 UI 与动画：Direct2D 硬件加速，归位动画、关闭淡出、悬停旋转",
            L"\u2022 设置面板：开机自启、标题前缀（最多 5 个汉字）、动画一键切换",
            L"\u2022 数据持久化：todos.json / config.json 即时写入，删除即可重置",
            L"\u2022 轻量单文件：约 400 KB 单 exe，免安装、免联网、无第三方依赖",
            L"\u2022 平台兼容：Windows 7 SP1+ / Windows 11，支持高 DPI",
            L"\u2022 开源免费：源码托管于 GitHub",
        };
        float contentW = dlgW - 32.0f;
        for (int i = 0; i < (int)(sizeof(feats) / sizeof(feats[0])); ++i) {
            IDWriteTextLayout* lay = nullptr;
            g.dw->CreateTextLayout(feats[i], (UINT32)wcslen(feats[i]), g.font(F_HINT), contentW, 200.0f, &lay);
            DWRITE_TEXT_METRICS tm; lay->GetMetrics(&tm); SafeRelease(lay);
            float rowH = tm.height + 8.0f;
            g.drawText(feats[i], D2D1::RectF(dlgX + 16, ty, dlgX + dlgW - 16, ty + rowH), F_HINT, D2D1::ColorF(C::DIALOG_AB.r, C::DIALOG_AB.g, C::DIALOG_AB.b, oa), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            ty += rowH;
        }
        g.rt->PopAxisAlignedClip();
        m_aboutContentH = (ty - sy) - dlgY + 10.0f;
        g.drawText(L"\u7248\u672c 2.5.10", D2D1::RectF(dlgX + 16, dlgY + dlgH - 34, dlgX + dlgW - 16, dlgY + dlgH - 20), F_FOOTER, D2D1::ColorF(C::DIALOG_FT.r, C::DIALOG_FT.g, C::DIALOG_FT.b, oa));
        g.drawText(kCopyright, D2D1::RectF(dlgX + 16, dlgY + dlgH - 20, dlgX + dlgW - 16, dlgY + dlgH - 6), F_FOOTER, D2D1::ColorF(C::DIALOG_FT.r, C::DIALOG_FT.g, C::DIALOG_FT.b, oa));
    }

    // App-rendered IME candidate list (the system IME window is hidden).
    if (m_editMode != ED_NONE && !m_cands.empty()) {
        FontId fid = F_TODO;
        if (m_editMode == ED_EDIT_PROJECT || m_editMode == ED_ADD_PROJECT) fid = F_PROJ_NAME;
        else if (m_editMode == ED_PREF_PREFIX) fid = F_SETTINGS;
        float textW = g.measureTextW(m_editText + m_compositionText, fid);
        float cx = m_editRectDip.left + textW;
        float cy = m_editRectDip.top + std::max(AppC::ROW_H, m_editRectDip.bottom - m_editRectDip.top) + 4.0f;
        if (m_editMode != ED_PREF_PREFIX) cy += AppC::TITLE_H - m_scroll;

        const float itemH = 20.0f, pad = 8.0f, numW = 20.0f;
        float popW = 110.0f;
        size_t shown = std::min(m_cands.size(), (size_t)9);
        for (size_t i = 0; i < shown; ++i)
            popW = std::max(popW, numW + g.measureTextW(m_cands[i], F_TODO) + 18.0f);
        float popH = pad * 2 + itemH * (float)shown;
        float x = std::max(4.0f, std::min(cx, W - popW - 4.0f));
        float y = cy;
        if (y + popH > H - 4.0f) y = std::max(4.0f, cy - popH - 8.0f);
        D2D1_RECT_F prc = D2D1::RectF(x, y, x + popW, y + popH);
        g.fillRoundedRect(prc, 6.0f, C::WHITE);
        g.strokeRoundedRect(prc, 6.0f, C::SEP, 1.0f);
        for (size_t i = 0; i < shown; ++i) {
            float iy = y + pad + (float)i * itemH;
            bool sel = (int)i == m_candSel;
            if (sel) {
                g.fillRoundedRect(D2D1::RectF(x + 3, iy - 2, x + popW - 3, iy + itemH - 2),
                                  4.0f, C::ACCENT);
            }
            std::wstring num = std::to_wstring(i + 1);
            g.drawText(num, D2D1::RectF(x + 8, iy, x + 8 + numW, iy + itemH), F_HINT,
                       sel ? C::WHITE : C::MUTED,
                       DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            g.drawText(m_cands[i], D2D1::RectF(x + 8 + numW, iy, x + popW - 6, iy + itemH), F_TODO,
                       sel ? C::WHITE : C::TEXT,
                       DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    if (g.rt->EndDraw() == D2DERR_RECREATE_TARGET) { g_gfx.recreate(m_hwnd); }
}
void App::tick(float dt) {
    // One-time fix shortly after launch: Explorer may build the desktop icon
    // layer after this window exists (typical with auto-start), which on Win10
    // can leave the tool window behind the icons. Re-raise once it has settled.
    if (!m_startupRaised) {
        m_startupT += dt;
        if (m_startupT >= 2.0f) {
            m_startupRaised = true;
            if (!m_closing) bringToFront();
        }
    }
    if (m_editMode != ED_NONE) {
        m_imeHideTimer += dt;
        if (m_imeHideTimer >= 0.25f) {
            m_imeHideTimer = 0;
            hideImeWindows();
        }
        m_pollImeTimer += dt;
        if (m_pollImeTimer >= 0.05f) {
            m_pollImeTimer = 0;
            pollIme();
        }
        m_cursorBlink += dt;
        if (m_edit) {
            // Rebuild layout when the edited text changes so rows wrap live
            // while typing (not only after committing with Enter).
            std::wstring curLayout = m_editText + m_compositionText;
            if (curLayout != m_lastEditLayout) {
                m_lastEditLayout = curLayout;
                rebuildHits();
                requestRedraw();
            }
            // Sync committed text from EDIT (skip during IME composition to avoid
            // overwriting the D2D-rendered pinyin with the EDIT's bare text).
            if (m_compositionText.empty()) {
                std::wstring cur = editGetText();
                if (cur != m_editText) {
                    m_editText = cur;
                    m_cursorBlink = 0;
                }
            }
            // Reposition EDIT at cursor (once per frame, no text overwrite).
            positionEdit();
        }
        requestRedraw();
    }
    approach(m_scroll, m_scrollTarget, dt, 16.0f);
    {
        float maxScroll = m_setContentH - (230.0f - 12.0f - 40.0f);
        if (maxScroll < 0) maxScroll = 0;
        if (m_setScrollTarget < 0) m_setScrollTarget = 0;
        if (m_setScrollTarget > maxScroll) m_setScrollTarget = maxScroll;
        approach(m_setScroll, m_setScrollTarget, dt, 16.0f);
    }
    {
        float maxScroll = m_aboutContentH - (230.0f - 12.0f - 40.0f);
        if (maxScroll < 0) maxScroll = 0;
        if (m_aboutScrollTarget < 0) m_aboutScrollTarget = 0;
        if (m_aboutScrollTarget > maxScroll) m_aboutScrollTarget = maxScroll;
        approach(m_aboutScroll, m_aboutScrollTarget, dt, 16.0f);
    }
    approach(m_overlayAlpha, (m_settings || m_about) ? 1.0f : 0.0f, dt, 14.0f);
    approach(m_aboutAlpha, m_about ? 1.0f : 0.0f, dt, 14.0f);
    approach(m_snapT, m_hoverSnap ? 1.0f : 0.0f, dt, 18.0f);
    approach(m_closeT, m_hoverClose ? 1.0f : 0.0f, dt, 18.0f);
    approach(m_addT, m_hoverAdd ? 1.0f : 0.0f, dt, 18.0f);
    approach(m_gearT, m_hoverGear ? 1.0f : 0.0f, dt, 18.0f);
    // Icon spin: one full turn on hover, spins back when the mouse leaves.
    if (m_hoverAdd && m_addSpin < 1.0f) m_addSpin = std::min(1.0f, m_addSpin + dt / 0.35f);
    else if (!m_hoverAdd && m_addSpin > 0.0f) m_addSpin = std::max(0.0f, m_addSpin - dt / 0.30f);
    if (m_hoverGear && m_gearSpin < 1.0f) m_gearSpin = std::min(1.0f, m_gearSpin + dt / 0.35f);
    else if (!m_hoverGear && m_gearSpin > 0.0f) m_gearSpin = std::max(0.0f, m_gearSpin - dt / 0.30f);
    if (m_hoverProjDel >= 0 && m_projDelSpin < 1.0f) m_projDelSpin = std::min(1.0f, m_projDelSpin + dt / 0.35f);
    else if (m_hoverProjDel < 0 && m_projDelSpin > 0.0f) m_projDelSpin = std::max(0.0f, m_projDelSpin - dt / 0.30f);
    approach(m_projDelT, m_hoverProjDel >= 0 ? 1.0f : 0.0f, dt, 18.0f);
    approach(m_circT, m_hovCircPi >= 0 ? 1.0f : 0.0f, dt, 20.0f);
    approach(m_checkT, m_hoverCheckbox ? 1.0f : 0.0f, dt, 18.0f);
    approach(m_aboutBtnT, m_hoverAboutBtn ? 1.0f : 0.0f, dt, 18.0f);

    bool needSave = false;
    for (auto it = m_fades.begin(); it != m_fades.end();) {
        it->alpha -= dt * 8.0f;
        it->off -= dt * 30.0f;
        if (it->alpha <= 0.0f) {
            if (it->pi >= 0 && it->pi < (int)m_projects.size()) {
                auto& ts = m_projects[it->pi].todos;
                if (it->ti >= 0 && it->ti < (int)ts.size()) {
                    // Record into the completed-task history before removing.
                    HistoryItem hi;
                    hi.project = m_projects[it->pi].name;
                    hi.text = ts[it->ti].text;
                    hi.created = ts[it->ti].created;
                    hi.completed = now_iso();
                    auto hist = load_history();
                    hist.push_back(hi);
                    save_history(hist);
                    ts.erase(ts.begin() + it->ti);
                    needSave = true;
                }
            }
            it = m_fades.erase(it);
        } else {
            ++it;
        }
    }
    if (needSave) { saveAll(); rebuildHits(); }
    if (m_snapping) {
        float dur = (m_cfg.snap_anim == 0) ? 0.01f : (m_cfg.snap_anim == 1) ? 0.80f
                   : (m_cfg.snap_anim == 2) ? 0.80f : (m_cfg.snap_anim == 3) ? 0.70f : 0.45f;
        m_snapAnim += dt / dur;
        if (m_snapAnim >= 1.0f) {
            m_snapAnim = 1.0f;
            m_snapping = false;
            if (m_cfg.snap_anim == 1) SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
            m_snapScale = 1.0f;
            m_snapRightArmed = false; m_snapBottomArmed = false;
        }
        float t = m_snapAnim;
        int x = m_snapToX, y = m_snapToY;
        m_snapScale = 1.0f;
        switch (m_cfg.snap_anim) {
            case 0:
                SetWindowPos(m_hwnd, nullptr, m_snapToX, m_snapToY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                break;
            case 2: {
                if (!m_snappedToTarget && t >= 0.5f) {
                    SetWindowPos(m_hwnd, nullptr, m_snapToX, m_snapToY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                    m_snappedToTarget = true;
                }
                if (m_snapAnim < 0.02f) {
                    LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
                    if (!(ex & WS_EX_LAYERED)) SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
                }
                float alphaF = (t < 0.5f) ? 1.0f - (t / 0.5f) * (t / 0.5f) : ((t - 0.5f) / 0.5f) * ((t - 0.5f) / 0.5f);
                SetLayeredWindowAttributes(m_hwnd, 0, (BYTE)(255.0f * alphaF), LWA_ALPHA);
                if (t >= 0.5f) {
                    float ease = 1.0f - (1.0f - (t - 0.5f) / 0.5f) * (1.0f - (t - 0.5f) / 0.5f);
                    float dist = (float)std::hypot((double)(m_snapToX - m_snapFromX), (double)(m_snapToY - m_snapFromY));
                    float lift = std::sin((t - 0.5f) / 0.5f * 3.14159265f) * dist * 0.15f;
                    x = (int)(m_snapFromX + (m_snapToX - m_snapFromX) * ease);
                    y = (int)(m_snapFromY + (m_snapToY - m_snapFromY) * ease - lift);
                    SetWindowPos(m_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
                m_snapScale = (t < 0.5f) ? 1.0f : (0.92f + 0.08f * ((t - 0.5f) / 0.5f));
                break;
            }
            case 3: {
                float elastic = 1.0f - std::pow(2.0f, -10.0f * t) * std::cos(t * 4.5f * 3.14159265f);
                float dist = (float)std::hypot((double)(m_snapToX - m_snapFromX), (double)(m_snapToY - m_snapFromY));
                float lift = std::sin(t * 3.14159265f) * dist * 0.18f;
                x = (int)(m_snapFromX + (m_snapToX - m_snapFromX) * elastic);
                y = (int)(m_snapFromY + (m_snapToY - m_snapFromY) * elastic - lift);
                SetWindowPos(m_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                break;
            }
            case 4: {
                float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
                float dist = (float)std::hypot((double)(m_snapToX - m_snapFromX), (double)(m_snapToY - m_snapFromY));
                float lift = std::sin(t * 3.14159265f) * dist * 0.20f;
                x = (int)(m_snapFromX + (m_snapToX - m_snapFromX) * ease);
                y = (int)(m_snapFromY + (m_snapToY - m_snapFromY) * ease - lift);
                SetWindowPos(m_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                break;
            }
            case 1: {
                RECT rc; GetWindowRect(m_hwnd, &rc);
                int wh = rc.bottom - rc.top;
                if (t < 0.5f) {
                    float gt = t / 0.5f;
                    float ease = gt * gt;
                    int flyDist = m_snapFromY + wh + 20;
                    x = m_snapFromX;
                    y = (int)(m_snapFromY - flyDist * ease);
                    SetWindowPos(m_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                } else {
                    if (!m_snappedToTarget) {
                        SetWindowPos(m_hwnd, nullptr, m_snapToX, m_snapToY + wh + 20, 0, 0,
                                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                        m_snappedToTarget = true;
                    }
                    float pt = (t - 0.5f) / 0.5f;
                    float ease = 1.0f - (1.0f - pt) * (1.0f - pt) * (1.0f - pt);
                    int startY = m_snapToY + wh + 20;
                    x = m_snapToX;
                    y = (int)(startY + (m_snapToY - startY) * ease);
                    SetWindowPos(m_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
                break;
            }
        }
    }
    if (m_closing) {
        if (m_closeAnim == 0.0f) {
            LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
            if (!(ex & WS_EX_LAYERED)) SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        }
        float fadeT = m_closeAnim < 0.15f ? 0.0f : (m_closeAnim - 0.15f) / 0.85f;
        BYTE alpha = (BYTE)(255.0f * (1.0f - fadeT * fadeT * fadeT * fadeT));
        SetLayeredWindowAttributes(m_hwnd, 0, alpha, LWA_ALPHA);
        m_closeAnim += dt / 0.45f;
        if (m_closeAnim >= 1.0f) {
            m_closing = false;
            DestroyWindow(m_hwnd);
            return;
        }
    }
    if (animating()) requestRedraw();
}

LRESULT App::onNcHitTest(POINT screenPt) {
    POINT pt = screenPt; ScreenToClient(m_hwnd, &pt);
    float x = toDip(pt.x), y = toDip(pt.y);
    float m = AppC::RESIZE_M;
    bool left = x < m, right = x > m_w - m;
    bool top = y < m, bottom = y > m_h - m;
    if (left && top) return HTTOPLEFT;
    if (right && top) return HTTOPRIGHT;
    if (left && bottom) return HTBOTTOMLEFT;
    if (right && bottom) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    if (y < AppC::TITLE_H && !m_settings && !m_about) {
        float closeW = 32.0f, snapW = 32.0f, gap = 8.0f, rightPad = 12.0f, btnH = 24.0f;
        float btnY1 = (AppC::TITLE_H - btnH) / 2.0f, btnY2 = btnY1 + btnH;
        float closeX = m_w - rightPad - closeW, snapX = closeX - gap - snapW;
        if (x >= snapX && x <= snapX + snapW && y >= btnY1 && y <= btnY2) return HTCLIENT;
        if (x >= closeX && x <= closeX + closeW && y >= btnY1 && y <= btnY2) return HTCLIENT;
        return HTCAPTION;
    }
    return HTCLIENT;
}
void App::onMoving(RECT* r) {
    int w = r->right - r->left, h = r->bottom - r->top;
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int dr = std::abs(r->right - wa.right), dl = std::abs(r->left - wa.left);
    int db = std::abs(r->bottom - wa.bottom), dt = std::abs(r->top - wa.top);
    // Direction-aware snap: only snap edges we're moving TOWARD
    // Use larger threshold on first drag from corner to avoid sticking
    float gap = AppC::SNAP_GAP;
    // Don't snap if window starts at the edge and user pulls away
    if (dr < gap && m_snapRightArmed)  { r->left = wa.right - w; r->right = wa.right; m_snapRightArmed = false; }
    if (dr > gap + 5.0f) { m_snapRightArmed = true; }
    if (dl < gap && m_snapLeftArmed)   { r->right = wa.left + w; r->left = wa.left; m_snapLeftArmed = false; }
    if (dl > gap + 5.0f) { m_snapLeftArmed = true; }
    if (db < gap && m_snapBottomArmed) { r->top = wa.bottom - h; r->bottom = wa.bottom; m_snapBottomArmed = false; }
    if (db > gap + 5.0f) { m_snapBottomArmed = true; }
    if (dt < gap && m_snapTopArmed)    { r->bottom = wa.top + h; r->top = wa.top; m_snapTopArmed = false; }
    if (dt > gap + 5.0f) { m_snapTopArmed = true; }
}
void App::onSizing(RECT* r) {
    int w = r->right - r->left, h = r->bottom - r->top;
    if (w < toPx(AppC::MIN_W)) r->right = r->left + toPx(AppC::MIN_W);
    if (h < toPx(AppC::MIN_H)) r->bottom = r->top + toPx(AppC::MIN_H);
}
void App::onMinMax(MINMAXINFO* mm) {
    mm->ptMinTrackSize.x = toPx(AppC::MIN_W);
    mm->ptMinTrackSize.y = toPx(AppC::MIN_H);
}
void App::onSize() {
    g_gfx.resize();
    m_w = g_gfx.clientW(); m_h = g_gfx.clientH();
    rebuildHits();
    clampScroll();
}
void App::onWheel(int delta) {
    if (m_about) {
        m_aboutScrollTarget -= (delta / 120.0f) * AppC::SCROLL_STEP;
        requestRedraw();
        return;
    }
    if (m_settings) {
        m_setScrollTarget -= (delta / 120.0f) * AppC::SCROLL_STEP;
        requestRedraw();
        return;
    }
    m_scrollTarget -= (delta / 120.0f) * AppC::SCROLL_STEP;
    clampScroll();
    requestRedraw();
}
void App::onChar(wchar_t ch) {
    if (m_editMode == ED_NONE) return;
    if (ch == L'\r' || ch == L'\n' || ch == 27) return;
    if (ch == L'\b') return;
    m_editText += ch;
    m_cursorBlink = 0;
    requestRedraw();
}
void App::onKeyDown(int vk) {
    if (m_editMode == ED_NONE) return;
    if (vk == VK_RETURN) { commitEdit(); return; }
    if (vk == VK_ESCAPE) {
        if (m_editMode == ED_PREF_PREFIX) { endEdit(false); closeSettings(); return; }
        cancelEdit();
        return;
    }
    // Backspace/delete handled by EDIT control
}
void App::onCompositionUpdate(const std::wstring& s) {
    if (m_editMode == ED_NONE) return;
    m_compositionText = s;
    m_cursorBlink = 0;
    requestRedraw();
}
void App::onCompositionResult(const std::wstring& s) {
    if (m_editMode == ED_NONE) return;
    (void)s;
    // The EDIT control updates its own text natively (TSF/IME writes straight
    // into the control), so the app only clears its composition copy.
    m_compositionText.clear();
    m_cursorBlink = 0;
    positionEdit();
    requestRedraw();
}
void App::onCompositionEnd() {
    if (m_editMode == ED_NONE) return;
    m_compositionText.clear();
    m_cands.clear();
    m_candSel = -1;
    positionEdit();
    requestRedraw();
}
static bool inRect(float x, float y, const D2D1_RECT_F& r) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}
void App::onMouseMove(float x, float y) {
    bool moved = (std::abs(x - m_lastMouseX) > 0.5f || std::abs(y - m_lastMouseY) > 0.5f);
    m_lastMouseX = x; m_lastMouseY = y;
    if (moved) m_suppressCircleHover = false;
    float W = m_w, H = m_h;
    float closeW = 32, snapW = 32, gap = 8, rightPad = 12, btnH = 24;
    float btnY1 = (AppC::TITLE_H - btnH) / 2, btnY2 = btnY1 + btnH;
    float closeX = W - rightPad - closeW, snapX = closeX - gap - snapW;
    int hsnap = (x >= snapX && x <= snapX + snapW && y >= btnY1 && y <= btnY2) ? 1 : 0;
    int hclose = (x >= closeX && x <= closeX + closeW && y >= btnY1 && y <= btnY2) ? 1 : 0;
    if (hsnap != m_hoverSnap) m_snapT = 0;
    if (hclose != m_hoverClose) m_closeT = 0;
    m_hoverSnap = hsnap; m_hoverClose = hclose;
    int hadd = (y > H - AppC::BOT_H && x >= 8 && x <= 44) ? 1 : 0;
    int hgear = (y > H - AppC::BOT_H && x >= W - 44 && x <= W - 8) ? 1 : 0;
    if (hadd != m_hoverAdd) m_addT = 0;
    if (hgear != m_hoverGear) m_gearT = 0;
    m_hoverAdd = hadd; m_hoverGear = hgear;
    rebuildHits();
    int hpd = -1; float contentY = y - AppC::TITLE_H + m_scroll;
    if (y < AppC::TITLE_H) {
        float delX = 0;
        for (size_t pi = 0; pi < m_projects.size(); ++pi) {
            (void)pi;
        }
    }
    bool overCircle = false;
    if (y >= AppC::TITLE_H && y <= H - AppC::BOT_H) {
        for (auto& h : m_hits) {
            if (contentY >= h.rc.top && contentY <= h.rc.bottom && x >= h.rc.left && x <= h.rc.right) {
                if (h.type == H_TODO_CIRCLE && !m_suppressCircleHover) {
                    if (h.pi != m_hovCircPi || h.ti != m_hovCircTi) { m_hovCircPi = h.pi; m_hovCircTi = h.ti; m_circT = 0; }
                    overCircle = true;
                }
                if (h.type == H_PROJ_DEL) {
                    if (h.pi != m_hoverProjDel) {
                        m_hoverProjDel = h.pi; m_projDelT = 0; m_projDelSpin = 0;
                    }
                    hpd = h.pi;
                }
                break;
            }
        }
    }
    // Clear the circle highlight when the mouse moves off it.
    if (!overCircle && m_hovCircPi >= 0 && !m_suppressCircleHover) {
        m_hovCircPi = -1; m_hovCircTi = -2; m_circT = 0;
    }
    if (hpd < 0) { if (m_hoverProjDel >= 0) m_projDelT = 0; m_hoverProjDel = -1; }
    if (m_suppressCircleHover) { if (m_hovCircPi >= 0) m_circT = 0; m_hovCircPi = -1; m_hovCircTi = -2; }
    requestRedraw(); // hover states change -> repaint so highlights appear
}
void App::onLButtonUp(float, float) {}
void App::onLButtonDblClk(float x, float y) {
    if (m_closing) return;
    float W = m_w, H = m_h;
    if (y >= AppC::TITLE_H && y <= H - AppC::BOT_H && !m_settings && !m_about) {
        float contentY = y - AppC::TITLE_H + m_scroll;
        rebuildHits();
        for (auto& h : m_hits) {
            if (contentY >= h.rc.top && contentY <= h.rc.bottom && x >= h.rc.left && x <= h.rc.right) {
                if (h.type == H_PROJ_NAME) {
                    if (m_editMode == ED_EDIT_PROJECT && m_editPi == h.pi)
                        setEditCaretFromPoint(x, contentY);
                    else
                        startEdit(ED_EDIT_PROJECT, h.pi, -1, m_projects[h.pi].name);
                    return;
                }
                if (h.type == H_TODO_TEXT) {
                    if (m_editMode == ED_EDIT_TODO && m_editPi == h.pi && m_editTi == h.ti)
                        setEditCaretFromPoint(x, contentY);
                    else
                        startEdit(ED_EDIT_TODO, h.pi, h.ti, m_projects[h.pi].todos[h.ti].text);
                    return;
                }
            }
        }
    }
}
void App::onLButtonDown(float x, float y) {
    if (m_closing) return;
    if (m_editMode == ED_PREF_PREFIX) {
        float dlgW = 220, dlgH = 230, dlgX = (m_w - dlgW) / 2, dlgY = (m_h - dlgH) / 2;
        float sy = -m_setScroll;
        float cy = dlgY + 14 + sy, cb = 18;
        float fy = cy + cb + 8 + 18;
        if (inRect(x, y, D2D1::RectF(dlgX + 16, fy, dlgX + dlgW - 16, fy + 26))) {
            m_cursorBlink = 0;
            m_editRectDip = D2D1::RectF(dlgX + 16, fy, dlgX + dlgW - 16, fy + 26);
            requestRedraw();
            return;
        }
        std::wstring t = editGetText();
        size_t a = t.find_first_not_of(L" \t\r\n");
        size_t b = t.find_last_not_of(L" \t\r\n");
        std::wstring trimmed = (a == std::wstring::npos) ? L"" : t.substr(a, b - a + 1);
        if (trimmed.size() > 5) trimmed = trimmed.substr(0, 5);
        m_cfg.title_prefix = trimmed;
        saveAll();
        endEdit(false);
    }
    float W = m_w, H = m_h;
    rebuildHits();
    if (m_about) {
        float dlgW = 220, dlgH = 230, dlgX = (W - dlgW) / 2, dlgY = (H - dlgH) / 2;
        if (!inRect(x, y, D2D1::RectF(dlgX, dlgY, dlgX + dlgW, dlgY + dlgH))) closeAbout();
        return;
    }
    if (m_settings) {
        float dlgW = 220, dlgH = 230, dlgX = (W - dlgW) / 2, dlgY = (H - dlgH) / 2;
        D2D1_RECT_F panel = D2D1::RectF(dlgX, dlgY, dlgX + dlgW, dlgY + dlgH);
        if (!inRect(x, y, panel)) { closeSettings(); return; }
        float sy = -m_setScroll;
        float cy = dlgY + 14 + sy, cb = 18, cbX = dlgX + 16;
        if (inRect(x, y, D2D1::RectF(cbX, cy, cbX + cb, cy + cb))) {
            m_cfg.auto_start = !m_cfg.auto_start;
            set_auto_start(m_cfg.auto_start);
            saveAll();
            requestRedraw();
            return;
        }
        float fy = cy + cb + 8 + 18;
        if (inRect(x, y, D2D1::RectF(dlgX + 16, fy, dlgX + dlgW - 16, fy + 26))) {
            beginEdit(ED_PREF_PREFIX, -1, -1, m_cfg.title_prefix);
            return;
        }
        float ay = fy + 26 + 10;
        float say = ay + 18.0f;
        float optW = 52, optH = 22, optGap = 8;
        int perRow = 3;
        for (int i = 0; i < 5; ++i) {
            int row = i / perRow, col = i % perRow;
            float ox = dlgX + 16 + col * (optW + optGap);
            float oy = say + row * (optH + optGap);
            if (inRect(x, y, D2D1::RectF(ox, oy, ox + optW, oy + optH))) {
                m_cfg.snap_anim = i; saveAll(); requestRedraw(); return;
            }
        }
        float py = say + 2 * optH + optGap + 6;
        float posW = 92, posH = 22, posGap = 8;
        for (int i = 0; i < 4; ++i) {
            int row = i / 2, col = i % 2;
            float ox = dlgX + 16 + col * (posW + posGap);
            float oy = py + 18 + row * (posH + posGap);
            if (inRect(x, y, D2D1::RectF(ox, oy, ox + posW, oy + posH))) {
                m_cfg.default_pos = i;
                saveAll();
                requestRedraw();
                return;
            }
        }
        float hy = py + 18 + 2 * posH + posGap + 6;
        if (inRect(x, y, D2D1::RectF(dlgX + 16, hy, dlgX + dlgW - 16, hy + 18))) {
            openHistory();
            return;
        }
        float aboutY = hy + 22;
        if (inRect(x, y, D2D1::RectF(dlgX + 16, aboutY, dlgX + dlgW - 16, aboutY + 18))) {
            openAbout();
            return;
        }
        return;
    }
    float closeW = 32, snapW = 32, gap = 8, rightPad = 12;
    float btnH = 24;
    float btnY1 = (AppC::TITLE_H - btnH) / 2, btnY2 = btnY1 + btnH;
    float closeX = W - rightPad - closeW, snapX = closeX - gap - snapW;
    if (y < AppC::TITLE_H) {
        if (inRect(x, y, D2D1::RectF(closeX, btnY1, closeX + closeW, btnY2))) { requestClose(); return; }
        if (inRect(x, y, D2D1::RectF(snapX, btnY1, snapX + snapW, btnY2))) {
            RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
            int pw = toPx(W), ph = toPx(H);
            int tx = wa.right - pw, ty = wa.bottom - ph;
            switch (m_cfg.default_pos) {
                case 1: tx = wa.left; ty = wa.top; break;          // 左上角
                case 2: tx = wa.left; ty = wa.bottom - ph; break;  // 左下角
                case 3: tx = wa.right - pw; ty = wa.top; break;    // 右上角
                default: break;                                     // 右下角
            }
            RECT cur; GetWindowRect(m_hwnd, &cur);
            m_snapFromX = cur.left; m_snapFromY = cur.top;
            m_snapToX = tx; m_snapToY = ty;
            m_snapAnim = 0; m_snapping = true; m_snappedToTarget = false;
            return;
        }
    }
    if (y > H - AppC::BOT_H) {
        if (inRect(x, y, D2D1::RectF(8, H - AppC::BOT_H, 44, H))) {
            startEdit(ED_ADD_PROJECT, -1, -1, L"");
            m_addingProject = true;
            m_scrollTarget = 1e9f; clampScroll();
            return;
        }
        if (inRect(x, y, D2D1::RectF(W - 44, H - AppC::BOT_H, W - 8, H))) {
            openSettings();
            return;
        }
        return;
    }
    if (y >= AppC::TITLE_H && y <= H - AppC::BOT_H) {
        float contentY = y - AppC::TITLE_H + m_scroll;
        for (auto& h : m_hits) {
            if (contentY >= h.rc.top && contentY <= h.rc.bottom && x >= h.rc.left && x <= h.rc.right) {
                switch (h.type) {
                    case H_TODO_CIRCLE: completeTodo(h.pi, h.ti); m_suppressCircleHover = true; m_hovCircPi = -1; m_hovCircTi = -2; return;
                    case H_TODO_TEXT:
                        if (m_editMode == ED_EDIT_TODO && m_editPi == h.pi && m_editTi == h.ti) {
                            if (m_compositionText.empty()) setEditCaretFromPoint(x, contentY);
                            return;
                        }
                        startEdit(ED_EDIT_TODO, h.pi, h.ti, m_projects[h.pi].todos[h.ti].text);
                        return;
                    case H_NEWTODO:
                        if (m_editMode == ED_NEW_TODO && m_editPi == h.pi) {
                            if (m_compositionText.empty()) setEditCaretFromPoint(x, contentY);
                            return;
                        }
                        startEdit(ED_NEW_TODO, h.pi, -1, L"");
                        return;
                    case H_PROJ_DEL: delProject(h.pi); return;
                    case H_PROJ_NAME:
                        if (m_editMode == ED_EDIT_PROJECT && m_editPi == h.pi)
                            setEditCaretFromPoint(x, contentY);
                        return;
                    default: return;
                }
            }
        }
    }
    if (!isEditing()) SetFocus(m_hwnd);
}
