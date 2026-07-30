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

static const wchar_t* kClass = L"ToDoWell2Class";
static UINT_PTR kTimer = 1;
static MMRESULT g_mmTimer = 0;
static HWND g_timerHwnd = nullptr;

static void CALLBACK mmTimerCb(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR) {
    if (g_timerHwnd) PostMessageW(g_timerHwnd, WM_TIMER, kTimer, 0);
}
static LARGE_INTEGER g_qpfFreq = {};
static LARGE_INTEGER g_qpfLast = {};

static double nowSec() {
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)g_qpfFreq.QuadPart;
}

static void ApplyRoundCorners(HWND hwnd) {
    bool ok = false;
    if (HMODULE m = LoadLibraryW(L"dwmapi.dll")) {
        typedef HRESULT(WINAPI* PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
        if (auto fn = (PFN_DwmSetWindowAttribute)GetProcAddress(m, "DwmSetWindowAttribute")) {
            int pref = 2;
            ok = SUCCEEDED(fn(hwnd, 33, &pref, sizeof(pref)));
        }
        FreeLibrary(m);
    }
    if (!ok) {
        RECT rc; GetWindowRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, 9, 9);
        SetWindowRgn(hwnd, rgn, TRUE);
        DeleteObject(rgn);
    }
}

static void SnapBottomRight(HWND hwnd, int pw, int ph) {
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(hwnd, nullptr, wa.right - pw, wa.bottom - ph, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

static int CalcWindowW() {
    const float wDip = 248.0f;
    return (int)(wDip * g_gfx.dpiScale + 0.5f);
}
static int CalcWindowH() {
    const float phi = 1.6180339887f;
    const float hDip = 256.0f * phi;
    return (int)(hDip * g_gfx.dpiScale + 0.5f);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_gfx.init(hwnd);
            g_app = new App();
            g_app->create(hwnd);
            ApplyRoundCorners(hwnd);
            QueryPerformanceFrequency(&g_qpfFreq);
            QueryPerformanceCounter(&g_qpfLast);
            timeBeginPeriod(1);
            g_timerHwnd = hwnd;
            g_mmTimer = timeSetEvent(1, 0, mmTimerCb, 0, TIME_PERIODIC);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            if (g_app) g_app->render();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_TIMER: {
            double now = nowSec();
            float dt = (float)(now - ((double)g_qpfLast.QuadPart / (double)g_qpfFreq.QuadPart));
            g_qpfLast.QuadPart = (LONGLONG)(now * (double)g_qpfFreq.QuadPart);
            if (dt > 0.1f) dt = 0.1f;
            if (g_app) g_app->tick(dt);
            return 0;
        }
        case WM_SIZE: {
            g_gfx.resize();
            if (g_app) g_app->onSize();
            ApplyRoundCorners(hwnd);
            return 0;
        }
        case WM_MOVE: {
            return 0;
        }
        case WM_NCCALCSIZE: {
            if (wp == TRUE) return 0;
            break;
        }
        case WM_NCACTIVATE: return TRUE;
        case WM_NCPAINT: return 0;
        case WM_NCHITTEST: {
            if (g_app) {
                POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                LRESULT ht = g_app->onNcHitTest(pt);
                if (ht != HTCLIENT) return ht;
                return HTCLIENT;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if (g_app) g_app->onMouseMove(g_app->toDip(GET_X_LPARAM(lp)), g_app->toDip(GET_Y_LPARAM(lp)));
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (g_app) g_app->onLButtonDown(g_app->toDip(GET_X_LPARAM(lp)), g_app->toDip(GET_Y_LPARAM(lp)));
            return 0;
        }
        case WM_LBUTTONUP: {
            if (g_app) g_app->onLButtonUp(g_app->toDip(GET_X_LPARAM(lp)), g_app->toDip(GET_Y_LPARAM(lp)));
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            if (g_app) g_app->onLButtonDblClk(g_app->toDip(GET_X_LPARAM(lp)), g_app->toDip(GET_Y_LPARAM(lp)));
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (g_app) g_app->onWheel(GET_WHEEL_DELTA_WPARAM(wp));
            return 0;
        }
        case WM_MOVING: if (g_app) { g_app->onMoving((RECT*)lp); return TRUE; } break;
        case WM_SIZING: if (g_app) { g_app->onSizing((RECT*)lp); return TRUE; } break;
        case WM_GETMINMAXINFO: if (g_app) { g_app->onMinMax((MINMAXINFO*)lp); return 0; } break;
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wp;
            SetBkColor(hdc, RGB(0xFF, 0xFF, 0xFF));
            SetTextColor(hdc, RGB(0x1d, 0x1d, 0x1f));
            static HBRUSH whiteBrush = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
            return (LRESULT)whiteBrush;
        }
        case WM_SETCURSOR: {
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                return TRUE;
            }
            break;
        }
        case WM_KEYDOWN: {
        if (g_app && g_app->isEditing()) {
            int vk = (int)wp;
            if (vk == VK_RETURN || vk == VK_ESCAPE) {
                g_app->onKeyDown(vk);
                return 0;
            }
            // Backspace/delete handled by EDIT child (has focus)
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
        case WM_CHAR: {
        if (g_app && !g_app->isEditing()) g_app->onChar((wchar_t)wp);
        return 0;
    }
        
        case WM_IME_SETCONTEXT:
            return DefWindowProcW(hwnd, msg, wp, lp);
        case WM_IME_STARTCOMPOSITION:
            return DefWindowProcW(hwnd, msg, wp, lp);
        case WM_IME_COMPOSITION:
            // Composition is polled from the EDIT's IME context in tick() every frame.
            // The main HWND's IME context is disassociated during editing (focus is on the EDIT child).
            return DefWindowProcW(hwnd, msg, wp, lp);
        case WM_IME_ENDCOMPOSITION:
            if (g_app) g_app->onCompositionEnd();
            return DefWindowProcW(hwnd, msg, wp, lp);
        case WM_IME_CHAR:
            if (g_app && g_app->isEditing()) {
                g_app->onChar((wchar_t)wp);
            }
            return 0;
        case WM_IME_NOTIFY:
            return 0;
        case WM_DESTROY: {
            if (g_app) { g_app->destroy(); delete g_app; g_app = nullptr; }
            if (g_mmTimer) { timeKillEvent(g_mmTimer); g_mmTimer = 0; }
            timeEndPeriod(1);
            g_gfx.cleanup();
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    SetProcessDPIAware();
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClass;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    {
        HDC hdc = GetDC(nullptr);
        int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        if (dpi <= 0) dpi = 96;
        g_gfx.dpiScale = dpi / 96.0f;
    }

    int pw = CalcWindowW(), ph = CalcWindowH();
    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        kClass, L"ToDoWell",
        WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_VISIBLE,
        0, 0, pw, ph,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 0;

    SnapBottomRight(hwnd, pw, ph);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
