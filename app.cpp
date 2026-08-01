#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <imm.h>
#include <cstdarg>
#include "gfx.h"
#include "app.h"
#include "resource.h"
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "imm32.lib")

static WNDPROC g_editOldProc = nullptr;
static HFONT g_fontTodo = nullptr;
static HFONT g_fontProj = nullptr;
static HBRUSH g_whiteBrush = nullptr;

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
bool App::animating() const {
    if (m_closing) return true;
    if (m_snapping) return true;
    // Keep repainting while the overlay shade fades in OR out, so the main
    // page never gets stuck dimmed after closing settings/about.
    if (m_overlayAlpha > 0.01f) return true;
    if (m_aboutAlpha > 0.01f) return true;
    if ((m_addSpin > 0.001f && m_addSpin < 1.0f) ||
        (m_gearSpin > 0.001f && m_gearSpin < 1.0f)) return true;
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
static void HideImeCompositionWindows() {
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
        LRESULT res = CallWindowProcW(g_editOldProc, h, msg, wp, lp);
        HideImeCompositionWindows();
        return res;
    } else if (msg == WM_IME_STARTCOMPOSITION) {
        HideImeCompositionWindows();
        return CallWindowProcW(g_editOldProc, h, msg, wp, lp);
    } else if (msg == WM_IME_COMPOSITION) {
        HideImeCompositionWindows();
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
void App::endEdit(bool applyFocus) {
    m_reentering = true;
    m_editMode = ED_NONE; m_editPi = -1; m_editTi = -1;
    m_compositionText.clear();
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
void App::positionEdit() {
    if (!m_edit || m_editMode == ED_NONE) return;
    if (m_editRectDip.left == 0 && m_editRectDip.right == 0) return;
    float editTop = m_editRectDip.top;
    if (m_editMode != ED_PREF_PREFIX) {
        editTop += AppC::TITLE_H - m_scroll;
    }
    // Real, visible input control aligned with the row: TSF/IME positions the
    // composition/candidate UI natively at the caret inside this control, and
    // the control paints its own text (styled to match the app).
    float editW = std::max(40.0f, m_editRectDip.right - m_editRectDip.left);
    int ex = toPx(m_editRectDip.left), ey = toPx(editTop);
    int ew = toPx(editW), eh = toPx(AppC::ROW_H);
    // Avoid repositioning every tick (1ms timer): a SetWindowPos storm disturbs
    // the IME's caret-geometry queries. Only move when something changed.
    if (ex != m_editPosX || ey != m_editPosY || ew != m_editPosW || eh != m_editPosH) {
        SetWindowPos(m_edit, nullptr, ex, ey, ew, eh, SWP_NOZORDER | SWP_NOACTIVATE);
        m_editPosX = ex; m_editPosY = ey; m_editPosW = ew; m_editPosH = eh;
    }
    ShowWindow(m_edit, SW_SHOWNOACTIVATE);
    HideCaret(m_edit); // D2D draws the visible caret
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
void App::onEditKillFocus() {}
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
                m_projects[pi].todos.push_back({trimmed, false});
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
    m_scrollTarget = 1e9f;
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

        for (size_t ti = 0; ti < proj.todos.size(); ++ti) {
            bool fading = false;
            for (auto& f : m_fades) if (f.pi == (int)pi && f.ti == (int)ti) { fading = true; break; }
            if (fading) { y += AppC::ROW_H; continue; }
            float rowTop = y;
            float cx = contentLeft + AppC::CARD_INNER + AppC::CIRCLE_R + 2.0f;
            float cy = rowTop + AppC::ROW_H / 2.0f;
            float textX = cx + AppC::CIRCLE_R + 8.0f;
            float textRight = contentLeft + contentW - AppC::CARD_INNER;
            m_hits.push_back({D2D1::RectF(cx - AppC::CIRCLE_R - 4, rowTop, cx + AppC::CIRCLE_R + 4, rowTop + AppC::ROW_H), H_TODO_CIRCLE, (int)pi, (int)ti});
            m_hits.push_back({D2D1::RectF(textX, rowTop, textRight, rowTop + AppC::ROW_H), H_TODO_TEXT, (int)pi, (int)ti});
            if (m_editMode == ED_EDIT_TODO && m_editPi == (int)pi && m_editTi == (int)ti) {
                m_editRectDip = D2D1::RectF(textX, rowTop, textRight, rowTop + AppC::ROW_H);
            }
            y = rowTop + AppC::ROW_H;
        }
        float ntTop = y;
        float cx = contentLeft + AppC::CARD_INNER + AppC::CIRCLE_R + 2.0f;
        float cy = ntTop + AppC::ROW_H / 2.0f;
        float textX = cx + AppC::CIRCLE_R + 8.0f;
        float textRight = contentLeft + contentW - AppC::CARD_INNER;
        m_hits.push_back({D2D1::RectF(contentLeft + AppC::CARD_INNER, ntTop, textRight, ntTop + AppC::ROW_H), H_NEWTODO, (int)pi, -1});
        if (m_editMode == ED_NEW_TODO && m_editPi == (int)pi) {
            m_editRectDip = D2D1::RectF(textX, ntTop, textRight, ntTop + AppC::ROW_H);
        }
        y = ntTop + AppC::ROW_H + 4.0f;
        float cardBottom = y + 6.0f;
        m_contentH = std::max(m_contentH, cardBottom + AppC::CARD_GAP);
        (void)cy; (void)screenY; (void)cx;
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

void App::render() {
    Gfx& g = g_gfx;
    float W = g.clientW(), H = g.clientH();
    g.rt->BeginDraw();
    g.rt->Clear(D2D1::ColorF(0, 0, 0, 0));

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
        // Card white background
        float cardContentBot = y + AppC::CARD_TOP + AppC::BADGE_H + 6.0f
                              + (float)proj.todos.size() * AppC::ROW_H
                              + AppC::ROW_H + 4.0f + 6.0f;
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
            float tw2 = g.measureTextW(m_editText, F_PROJ_NAME);
            float cw = g.measureTextW(m_compositionText, F_PROJ_NAME);
            if (((int)(m_cursorBlink * 2) % 2) == 0) g.drawLine(nameX + tw2 + cw + 1, screenHy + 2, nameX + tw2 + cw + 1, screenHy + AppC::BADGE_H - 2, C::ACCENT, 1.5f);
        } else {
            g.drawText(proj.name, D2D1::RectF(nameX, screenHy, nameRight, screenHy + AppC::BADGE_H), F_PROJ_NAME, C::TEXT);
        }
        float delX = contentLeft + contentW - AppC::CARD_INNER - delW;
        D2D1_COLOR_F delCol = lerpColor(C::DEL_BTN, C::DEL_HOVER, m_projDelT);
        g.drawText(L"\u2715", D2D1::RectF(delX, screenHy, delX + delW, screenHy + AppC::BADGE_H), F_SYM_TITLE, delCol,
                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        y = hy + AppC::BADGE_H + 6.0f;

        for (size_t ti = 0; ti < proj.todos.size(); ++ti) {
            bool fading = false;
            float fa = 1.0f, fo = 0.0f;
            for (auto& f : m_fades) if (f.pi == (int)pi && f.ti == (int)ti) { fading = true; fa = f.alpha; fo = f.off; break; }
            if (fading) {
                float cx = contentLeft + AppC::CARD_INNER + AppC::CIRCLE_R + 2.0f;
                float cy = y + AppC::ROW_H / 2.0f;
                float textX = cx + AppC::CIRCLE_R + 8.0f;
                float textRight = contentLeft + contentW - AppC::CARD_INNER;
                float sy = cTop + y - scrollOff + fo;
                D2D1_COLOR_F cc = D2D1::ColorF(C::ACCENT.r, C::ACCENT.g, C::ACCENT.b, fa);
                g.drawEllipse(cx, cy + (cTop - scrollOff) + fo, AppC::CIRCLE_R, AppC::CIRCLE_R, cc, 1.8f);
                g.rt->SetTransform(D2D1::Matrix3x2F::Identity());
                D2D1_COLOR_F tc = D2D1::ColorF(C::TEXT.r, C::TEXT.g, C::TEXT.b, fa);
                g.drawText(proj.todos[ti].text, D2D1::RectF(textX, cTop + y - scrollOff + fo, textRight, cTop + y - scrollOff + fo + AppC::ROW_H), F_TODO, tc,
                           DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                (void)fa; (void)fo;
                y += AppC::ROW_H;
                continue;
            }
            float rowTop = y;
            float cx = contentLeft + AppC::CARD_INNER + AppC::CIRCLE_R + 2.0f;
            float cy = rowTop + AppC::ROW_H / 2.0f;
            float textX = cx + AppC::CIRCLE_R + 8.0f;
            float textRight = contentLeft + contentW - AppC::CARD_INNER;
            float sy = cTop + rowTop - scrollOff;
            bool hov = (m_hovCircPi == (int)pi && m_hovCircTi == (int)ti);
            D2D1_COLOR_F cc = hov ? C::ACCENT : C::CIRCLE;
            g.drawEllipse(cx, cy + (cTop - scrollOff), AppC::CIRCLE_R, AppC::CIRCLE_R, cc, 1.8f);
            if (!(m_editMode == ED_EDIT_TODO && m_editPi == (int)pi && m_editTi == (int)ti)) {
                g.drawText(proj.todos[ti].text, D2D1::RectF(textX, sy, textRight, sy + AppC::ROW_H), F_TODO, C::TEXT,
                           DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            } else {
                std::wstring dtext = m_editText + m_compositionText;
                g.drawText(dtext, D2D1::RectF(textX, sy, textRight, sy + AppC::ROW_H), F_TODO, C::TEXT,
                           DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                float tw2 = g.measureTextW(m_editText, F_TODO);
                float cw = g.measureTextW(m_compositionText, F_TODO);
                if (((int)(m_cursorBlink * 2) % 2) == 0) g.drawLine(textX + tw2 + cw + 1, sy + 4, textX + tw2 + cw + 1, sy + AppC::ROW_H - 4, C::ACCENT, 1.5f);
            }
            y += AppC::ROW_H;
        }
        {
            float ntTop = y;
            float cx = contentLeft + AppC::CARD_INNER + AppC::CIRCLE_R + 2.0f;
            float cy = ntTop + AppC::ROW_H / 2.0f;
            float textX = cx + AppC::CIRCLE_R + 8.0f;
            float textRight = contentLeft + contentW - AppC::CARD_INNER;
            float sy = cTop + ntTop - scrollOff;
            g.drawEllipse(cx, cy + (cTop - scrollOff), AppC::CIRCLE_R, AppC::CIRCLE_R, C::CIRCLE, 1.5f);
            if (m_editMode == ED_NEW_TODO && m_editPi == (int)pi) {
                g.drawLine(textX, sy + AppC::ROW_H - 2, textRight, sy + AppC::ROW_H - 2, C::ACCENT, 1.5f);
                std::wstring dtext = m_editText + m_compositionText;
                g.drawText(dtext, D2D1::RectF(textX, sy, textRight, sy + AppC::ROW_H), F_TODO, C::TEXT,
                           DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                float tw2 = g.measureTextW(m_editText, F_TODO);
                float cw = g.measureTextW(m_compositionText, F_TODO);
                if (((int)(m_cursorBlink * 2) % 2) == 0) g.drawLine(textX + tw2 + cw + 1, sy + 4, textX + tw2 + cw + 1, sy + AppC::ROW_H - 4, C::ACCENT, 1.5f);
            }
        }
        y += AppC::ROW_H + 4.0f + 6.0f + AppC::CARD_GAP;

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
            float tw2 = g.measureTextW(m_editText, F_PROJ_NAME);
            float cw = g.measureTextW(m_compositionText, F_PROJ_NAME);
            bool showCursor = ((int)(m_cursorBlink * 2) % 2) == 0;
            if (showCursor) g.drawLine(ex + tw2 + cw + 1, screenTop + 2, ex + tw2 + cw + 1, screenTop + 29, C::ACCENT, 1.5f);
        }
    }
    g.rt->PopAxisAlignedClip();

    // bottom bar
    g.fillRect(D2D1::RectF(0, H - AppC::BOT_H, W, H), C::PAGE);
    D2D1_COLOR_F addCol = lerpColor(C::TEXT, C::ACCENT, m_addT);
    D2D1_POINT_2F addC = D2D1::Point2F(26.0f, H - AppC::BOT_H / 2.0f);
    g.rt->SetTransform(D2D1::Matrix3x2F::Rotation(m_addSpin * 360.0f, addC));
    g.drawText(L"+", D2D1::RectF(8, H - AppC::BOT_H, 44, H), F_SYM_BOTTOM, addCol,
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    g.rt->SetTransform(D2D1::Matrix3x2F::Identity());
    D2D1_COLOR_F gearCol = lerpColor(C::TEXT, C::ACCENT, m_gearT);
    D2D1_POINT_2F gearC = D2D1::Point2F(W - 26.0f, H - AppC::BOT_H / 2.0f);
    g.rt->SetTransform(D2D1::Matrix3x2F::Rotation(m_gearSpin * 360.0f, gearC));
    g.drawText(L"\u2699", D2D1::RectF(W - 44, H - AppC::BOT_H, W - 8, H), F_SYM_BOTTOM, gearCol,
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
        ay = say;
        D2D1_COLOR_F aboutCol = D2D1::ColorF(C::ACCENT.r, C::ACCENT.g, C::ACCENT.b, oa);
        g.drawText(L"\u5173\u4e8e ToDoWell", D2D1::RectF(dlgX + 16, ay, dlgX + dlgW - 16, ay + 18), F_HINT, aboutCol);
        g.rt->PopAxisAlignedClip();
        g.drawText(L"\u7248\u672c 2.0.0", D2D1::RectF(dlgX + 16, dlgY + dlgH - 34, dlgX + dlgW - 16, dlgY + dlgH - 20), F_FOOTER, D2D1::ColorF(C::DIALOG_FT.r, C::DIALOG_FT.g, C::DIALOG_FT.b, oa));
        g.drawText(L"\u7248\u6743\u6240\u6709@\u5929\u624d\u76845014", D2D1::RectF(dlgX + 16, dlgY + dlgH - 20, dlgX + dlgW - 16, dlgY + dlgH - 6), F_FOOTER, D2D1::ColorF(C::DIALOG_FT.r, C::DIALOG_FT.g, C::DIALOG_FT.b, oa));
        if (m_editMode == ED_PREF_PREFIX) m_editRectDip = D2D1::RectF(dlgX + 16, fy, dlgX + dlgW - 16, fy + 26);
        m_setContentH = (ay + 20.0f + sy) - dlgY + 10.0f;
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
            L"\u2022 \u70b9\u51fb + \u65b0\u5efa\u9879\u76ee\uff0c\u56de\u8f66\u786e\u8ba4\u540d\u79f0",
            L"\u2022 \u5728\u9879\u76ee\u4e0b\u6309\u56de\u8f66\u5373\u53ef\u65b0\u589e\u5f85\u529e\u4efb\u52a1",
            L"\u2022 \u70b9\u51fb\u4efb\u52a1\u524d\u5706\u5708\u6807\u8bb0\u5b8c\u6210\u5e76\u81ea\u52a8\u6d88\u5931",
            L"\u2022 \u53cc\u51fb\u9879\u76ee\u540d\u6216\u4efb\u52a1\u6587\u5b57\u53ef\u7f16\u8f91\u4fee\u6539",
            L"\u2022 \u8bbe\u7f6e\u4e2d\u4e00\u952e\u5f00\u542f\u6216\u5173\u95ed\u5f00\u673a\u81ea\u542f",
            L"\u2022 \u652f\u6301\u81ea\u5b9a\u4e49\u6807\u9898\u524d\u7f00 + gogogo!!!",
            L"\u2022 \u6240\u6709\u6570\u636e\u81ea\u52a8\u4fdd\u5b58\uff0c\u91cd\u542f\u8f6f\u4ef6\u4e0d\u4e22\u5931",
            L"\u2022 \u8f6f\u4ef6\u57fa\u4e8e C++ \u548c Direct2D \u7f16\u8bd1",
            L"\u2022 \u652f\u6301\u591a\u79cd\u5f52\u4f4d\u52a8\u753b\uff0c\u53ef\u5728\u8bbe\u7f6e\u4e2d\u5207\u6362\u9009\u62e9",
        };
        float contentW = dlgW - 32.0f;
        for (int i = 0; i < 9; ++i) {
            IDWriteTextLayout* lay = nullptr;
            g.dw->CreateTextLayout(feats[i], (UINT32)wcslen(feats[i]), g.font(F_HINT), contentW, 200.0f, &lay);
            DWRITE_TEXT_METRICS tm; lay->GetMetrics(&tm); SafeRelease(lay);
            float rowH = tm.height + 8.0f;
            g.drawText(feats[i], D2D1::RectF(dlgX + 16, ty, dlgX + dlgW - 16, ty + rowH), F_HINT, D2D1::ColorF(C::DIALOG_AB.r, C::DIALOG_AB.g, C::DIALOG_AB.b, oa), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            ty += rowH;
        }
        g.rt->PopAxisAlignedClip();
        m_aboutContentH = (ty - sy) - dlgY + 10.0f;
        g.drawText(L"\u7248\u672c 2.0.0", D2D1::RectF(dlgX + 16, dlgY + dlgH - 34, dlgX + dlgW - 16, dlgY + dlgH - 20), F_FOOTER, D2D1::ColorF(C::DIALOG_FT.r, C::DIALOG_FT.g, C::DIALOG_FT.b, oa));
        g.drawText(L"\u7248\u6743\u6240\u6709@\u5929\u624d\u76845014", D2D1::RectF(dlgX + 16, dlgY + dlgH - 20, dlgX + dlgW - 16, dlgY + dlgH - 6), F_FOOTER, D2D1::ColorF(C::DIALOG_FT.r, C::DIALOG_FT.g, C::DIALOG_FT.b, oa));
    }

    // App-rendered IME candidate list (the system IME window is hidden).
    if (m_editMode != ED_NONE && !m_cands.empty()) {
        FontId fid = F_TODO;
        if (m_editMode == ED_EDIT_PROJECT || m_editMode == ED_ADD_PROJECT) fid = F_PROJ_NAME;
        else if (m_editMode == ED_PREF_PREFIX) fid = F_SETTINGS;
        float textW = g.measureTextW(m_editText + m_compositionText, fid);
        float cx = m_editRectDip.left + textW;
        float cy = m_editRectDip.top + AppC::ROW_H + 4.0f;
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
    if (m_editMode != ED_NONE) {
        m_imeHideTimer += dt;
        if (m_imeHideTimer >= 0.25f) {
            m_imeHideTimer = 0;
            HideImeCompositionWindows();
        }
        m_pollImeTimer += dt;
        if (m_pollImeTimer >= 0.05f) {
            m_pollImeTimer = 0;
            pollIme();
        }
        m_cursorBlink += dt;
        if (m_edit) {
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
    if (y >= AppC::TITLE_H && y <= H - AppC::BOT_H) {
        for (auto& h : m_hits) {
            if (contentY >= h.rc.top && contentY <= h.rc.bottom && x >= h.rc.left && x <= h.rc.right) {
                if (h.type == H_TODO_CIRCLE && !m_suppressCircleHover) {
                    if (h.pi != m_hovCircPi || h.ti != m_hovCircTi) { m_hovCircPi = h.pi; m_hovCircTi = h.ti; m_circT = 0; }
                }
                if (h.type == H_PROJ_DEL) {
                    if (h.pi != m_hoverProjDel) { m_hoverProjDel = h.pi; m_projDelT = 0; }
                    hpd = h.pi;
                }
                break;
            }
        }
    }
    if (hpd < 0) { if (m_hoverProjDel >= 0) m_projDelT = 0; m_hoverProjDel = -1; }
    if (m_suppressCircleHover) { if (m_hovCircPi >= 0) m_circT = 0; m_hovCircPi = -1; m_hovCircTi = -2; }
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
                if (h.type == H_PROJ_NAME) { beginEdit(ED_EDIT_PROJECT, h.pi, -1, m_projects[h.pi].name); return; }
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
        ay = say + 2 * optH + optGap + 6;
        if (inRect(x, y, D2D1::RectF(dlgX + 16, ay, dlgX + dlgW - 16, ay + 18))) {
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
            RECT cur; GetWindowRect(m_hwnd, &cur);
            m_snapFromX = cur.left; m_snapFromY = cur.top;
            m_snapToX = wa.right - pw; m_snapToY = wa.bottom - ph;
            m_snapAnim = 0; m_snapping = true; m_snappedToTarget = false;
            return;
        }
    }
    if (y > H - AppC::BOT_H) {
        if (inRect(x, y, D2D1::RectF(8, H - AppC::BOT_H, 44, H))) {
            m_addingProject = true;
            beginEdit(ED_ADD_PROJECT, -1, -1, L"");
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
                    case H_TODO_TEXT: beginEdit(ED_EDIT_TODO, h.pi, h.ti, m_projects[h.pi].todos[h.ti].text); return;
                    case H_NEWTODO: beginEdit(ED_NEW_TODO, h.pi, -1, L""); return;
                    case H_PROJ_DEL: delProject(h.pi); return;
                    default: return;
                }
            }
        }
    }
    if (!isEditing()) SetFocus(m_hwnd);
}
