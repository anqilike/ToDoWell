#include "gfx.h"

Gfx g_gfx;

static IDWriteTextFormat* makeFont(IDWriteFactory* dw, const wchar_t* family,
                                   DWRITE_FONT_WEIGHT w, float sizeDip) {
    IDWriteTextFormat* f = nullptr;
    dw->CreateTextFormat(family, nullptr, w, DWRITE_FONT_STYLE_NORMAL,
                         DWRITE_FONT_STRETCH_NORMAL, sizeDip, L"zh-CN", &f);
    return f;
}

bool Gfx::init(HWND hwnd) {
    this->hwnd = hwnd;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&dw)))) return false;

    // DPI: GetDeviceCaps works on Win7+ (GetDpiForWindow is Win10 1607+).
    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(nullptr, hdc);
    if (dpi <= 0) dpi = 96;
    dpiScale = dpi / 96.0f;

    RECT rc; GetClientRect(hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_HARDWARE, // GPU-accelerated (RTX A4000 on target Win7)
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    D2D1_HWND_RENDER_TARGET_PROPERTIES hp =
        D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right > 0 ? rc.right : 1, rc.bottom > 0 ? rc.bottom : 1));
    if (FAILED(d2d->CreateHwndRenderTarget(rtp, hp, &rt)) || !rt) return false;
    // Leave rt DPI at desktop default so 1 DIP maps to physical px via dpiScale.
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush);

    // Fonts (pt -> DIPs).
    const wchar_t* YH = L"Microsoft YaHei";
    const wchar_t* SY = L"Segoe UI Symbol";
    fonts[F_TITLE_BIG]   = makeFont(dw, YH, DWRITE_FONT_WEIGHT_BOLD, pt(11));
    fonts[F_TITLE_PRE]   = makeFont(dw, YH, DWRITE_FONT_WEIGHT_BOLD, pt(11));
    fonts[F_PROJ_NAME]   = makeFont(dw, YH, DWRITE_FONT_WEIGHT_BOLD, pt(12));
    fonts[F_PROJ_NUM]    = makeFont(dw, YH, DWRITE_FONT_WEIGHT_BOLD, pt(9));
    fonts[F_TODO]        = makeFont(dw, YH, DWRITE_FONT_WEIGHT_NORMAL, pt(10));
    fonts[F_TODO_PH]     = makeFont(dw, YH, DWRITE_FONT_WEIGHT_NORMAL, pt(10));
    fonts[F_SETTINGS]    = makeFont(dw, YH, DWRITE_FONT_WEIGHT_NORMAL, pt(10));
    fonts[F_HINT]        = makeFont(dw, YH, DWRITE_FONT_WEIGHT_NORMAL, pt(9));
    fonts[F_ABOUT_TITLE] = makeFont(dw, YH, DWRITE_FONT_WEIGHT_BOLD, pt(14));
    fonts[F_FOOTER]      = makeFont(dw, YH, DWRITE_FONT_WEIGHT_NORMAL, pt(8));
    fonts[F_SYM_TITLE]   = makeFont(dw, SY, DWRITE_FONT_WEIGHT_NORMAL, pt(11));
    fonts[F_SYM_BOTTOM]  = makeFont(dw, SY, DWRITE_FONT_WEIGHT_NORMAL, pt(17));
    fonts[F_SYM_CIRCLE]  = makeFont(dw, SY, DWRITE_FONT_WEIGHT_NORMAL, pt(16));
    fonts[F_SYM_SMALL]   = makeFont(dw, SY, DWRITE_FONT_WEIGHT_NORMAL, pt(9));
    fonts[F_SYM_CHECK]   = makeFont(dw, SY, DWRITE_FONT_WEIGHT_NORMAL, pt(12));
    return true;
}

void Gfx::resize() {
    if (!rt) return;
    RECT rc; GetClientRect(hwnd, &rc);
    D2D1_SIZE_U sz = D2D1::SizeU(rc.right > 0 ? rc.right : 1, rc.bottom > 0 ? rc.bottom : 1);
    rt->Resize(sz);
}

bool Gfx::recreate(HWND hwnd) {
    cleanup();
    return init(hwnd);
}

void Gfx::cleanup() {
    for (int i = 0; i < F_COUNT; ++i) SafeRelease(fonts[i]);
    SafeRelease(brush);
    SafeRelease(rt);
    SafeRelease(dw);
    SafeRelease(d2d);
}

IDWriteTextFormat* Gfx::font(FontId id) {
    if (id < 0 || id >= F_COUNT) return fonts[F_TODO];
    if (!fonts[id]) return fonts[F_TODO];
    return fonts[id];
}

void Gfx::fillRect(D2D1_RECT_F rc, D2D1_COLOR_F c) { setColor(c); rt->FillRectangle(rc, brush); }
void Gfx::fillRoundedRect(D2D1_RECT_F rc, float r, D2D1_COLOR_F c) {
    setColor(c); rt->FillRoundedRectangle(D2D1::RoundedRect(rc, r, r), brush);
}
void Gfx::strokeRect(D2D1_RECT_F rc, D2D1_COLOR_F c, float w) {
    setColor(c); rt->DrawRectangle(rc, brush, w);
}
void Gfx::strokeRoundedRect(D2D1_RECT_F rc, float r, D2D1_COLOR_F c, float w) {
    setColor(c); rt->DrawRoundedRectangle(D2D1::RoundedRect(rc, r, r), brush, w);
}
void Gfx::fillEllipse(float cx, float cy, float rx, float ry, D2D1_COLOR_F c) {
    setColor(c); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), brush);
}
void Gfx::drawEllipse(float cx, float cy, float rx, float ry, D2D1_COLOR_F c, float w) {
    setColor(c); rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), brush, w);
}
void Gfx::drawLine(float x1, float y1, float x2, float y2, D2D1_COLOR_F c, float w) {
    setColor(c); rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush, w);
}
void Gfx::drawText(const std::wstring& s, D2D1_RECT_F rc, FontId fid, D2D1_COLOR_F c,
                   DWRITE_TEXT_ALIGNMENT ta, DWRITE_PARAGRAPH_ALIGNMENT pa) {
    if (s.empty()) return;
    IDWriteTextFormat* f = font(fid);
    f->SetTextAlignment(ta);
    f->SetParagraphAlignment(pa);
    setColor(c);
    rt->DrawTextW(s.c_str(), (UINT32)s.size(), f, rc, brush,
                  D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
}
float Gfx::measureTextW(const std::wstring& s, FontId fid, float maxWidth) {
    if (s.empty()) return 0;
    IDWriteTextLayout* lay = nullptr;
    if (FAILED(dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), font(fid), maxWidth, 1000.0f, &lay)) || !lay)
        return 0;
    DWRITE_TEXT_METRICS m; lay->GetMetrics(&m);
    SafeRelease(lay);
    return m.width;
}
