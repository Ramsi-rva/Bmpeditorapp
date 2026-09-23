// gui_main.cpp
// BmpEditor GUI - aplicacion nativa de Windows (Win32 + GDI puro, sin
// librerias de terceros) que permite editar imagenes BMP visualmente:
//
//   - Abrir un archivo BMP con el dialogo nativo de Windows.
//   - Ver la imagen en un lienzo (canvas) dentro de la ventana.
//   - Hacer clic en un pixel del lienzo para seleccionarlo y ver/cambiar
//     su color (con una rueda de color HSV o con el selector nativo de
//     Windows, o escribiendo los valores R/G/B a mano).
//   - Arrastrar el mouse sobre el lienzo para definir un area y usarla
//     para recortar la imagen o dibujar un rectangulo relleno/con borde.
//   - Aplicar todas las operaciones ya existentes en la version de
//     consola: escala de grises, invertir, flip horizontal/vertical,
//     rotar 90/180, brillo, contraste, redimensionar y reemplazar color.
//
// Esta ventana reutiliza exactamente la misma biblioteca de bajo nivel
// (bmp_image.hpp/.cpp) que la version de consola (bmpedit.exe): la GUI
// solo agrega la capa de presentacion e interaccion con el mouse.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

#include "bmp_image.hpp"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

using namespace bmpeditor;

// ============================================================================
// Identificadores de controles
// ============================================================================
enum ControlId : int {
    ID_FILE_OPEN = 1001,
    ID_FILE_SAVE,
    ID_FILE_SAVEAS,
    ID_FILE_EXIT,
    ID_EDIT_UNDO,

    ID_BTN_GRAYSCALE = 1100,
    ID_BTN_INVERT,
    ID_BTN_FLIPH,
    ID_BTN_FLIPV,
    ID_BTN_ROTATE90,
    ID_BTN_ROTATE180,

    ID_EDIT_PIXEL_R = 1200,
    ID_EDIT_PIXEL_G,
    ID_EDIT_PIXEL_B,
    ID_BTN_APPLY_PIXEL_COLOR,
    ID_BTN_CHOOSE_COLOR_DIALOG,
    ID_SWATCH_PIXEL,

    ID_COLORWHEEL = 1300,
    ID_SWATCH_BRUSH,

    ID_BTN_MODE_SELECT = 1400,
    ID_BTN_MODE_CROP,
    ID_BTN_MODE_RECT,
    ID_EDIT_SEL_X,
    ID_EDIT_SEL_Y,
    ID_EDIT_SEL_W,
    ID_EDIT_SEL_H,
    ID_CHK_FILL,
    ID_BTN_APPLY_SELECTION,

    ID_EDIT_RESIZE_W = 1500,
    ID_EDIT_RESIZE_H,
    ID_BTN_APPLY_RESIZE,

    ID_LABEL_BRIGHTNESS = 1600,
    ID_TRACKBAR_BRIGHTNESS,
    ID_BTN_APPLY_BRIGHTNESS,
    ID_LABEL_CONTRAST,
    ID_TRACKBAR_CONTRAST,
    ID_BTN_APPLY_CONTRAST,

    ID_SWATCH_SRC = 1700,
    ID_BTN_PICK_SRC_COLOR,
    ID_SWATCH_DST,
    ID_BTN_PICK_DST_COLOR,
    ID_EDIT_TOLERANCE,
    ID_BTN_REPLACE,

    ID_CANVAS = 1800,
    ID_STATUS = 1900,
};

// ============================================================================
// Ventana: 1180 x 890 (area de cliente). Vease docs/GUI.md para el diagrama
// de layout completo.
// ============================================================================
static const int MAIN_WIDTH = 1180;
static const int MAIN_HEIGHT = 890;

// ============================================================================
// Estado global de la aplicacion (una sola ventana/documento a la vez).
// ============================================================================
struct AppState {
    std::unique_ptr<BmpImage> image;
    std::unique_ptr<BmpImage> original;  // copia para "Deshacer todo"
    std::wstring currentPath;
    bool unsavedChanges = false;

    enum class Mode { Select, Crop, Rect } mode = Mode::Select;

    // Pixel seleccionado (modo Select)
    int selX = -1, selY = -1;

    // Rectangulo de seleccion confirmado (modo Crop/Rect), en coords de imagen
    bool hasSelectionRect = false;
    int selRectX = 0, selRectY = 0, selRectW = 0, selRectH = 0;

    // Arrastre en curso sobre el lienzo
    bool dragging = false;
    int dragStartX = 0, dragStartY = 0;
    int dragCurX = 0, dragCurY = 0;

    COLORREF brushColor = RGB(255, 0, 0);  // color elegido en la rueda / usado para Rectangulo
    COLORREF srcColor = RGB(255, 0, 0);    // color origen para Reemplazar
    COLORREF dstColor = RGB(0, 255, 0);    // color destino para Reemplazar
};

static AppState g;
static HINSTANCE g_hInstance = nullptr;
static HWND g_hMainWnd = nullptr;
static HWND g_hCanvas = nullptr;
static HWND g_hColorWheel = nullptr;
static HWND g_hStatus = nullptr;
static HWND g_hEditR = nullptr, g_hEditG = nullptr, g_hEditB = nullptr;
static HWND g_hSwatchPixel = nullptr, g_hSwatchBrush = nullptr;
static HWND g_hSwatchSrc = nullptr, g_hSwatchDst = nullptr;
static HWND g_hEditSelX = nullptr, g_hEditSelY = nullptr, g_hEditSelW = nullptr, g_hEditSelH = nullptr;
static HWND g_hEditResizeW = nullptr, g_hEditResizeH = nullptr;
static HWND g_hTrackBrightness = nullptr, g_hTrackContrast = nullptr;
static HWND g_hLabelBrightness = nullptr, g_hLabelContrast = nullptr;
static HWND g_hChkFill = nullptr;
static HWND g_hEditTolerance = nullptr;

// ============================================================================
// Utilidades: conversion de cadenas y de HSV a RGB
// ============================================================================

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], len);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], len, nullptr, nullptr);
    return s;
}

void SetStatus(const std::wstring& text) {
    if (g_hStatus) SetWindowTextW(g_hStatus, text.c_str());
}

// h en [0,360), s y v en [0,1]
COLORREF HSVtoRGB(double h, double s, double v) {
    double c = v * s;
    double hp = h / 60.0;
    double x = c * (1 - std::fabs(std::fmod(hp, 2.0) - 1));
    double r1 = 0, g1 = 0, b1 = 0;
    if (hp >= 0 && hp < 1) { r1 = c; g1 = x; b1 = 0; }
    else if (hp < 2) { r1 = x; g1 = c; b1 = 0; }
    else if (hp < 3) { r1 = 0; g1 = c; b1 = x; }
    else if (hp < 4) { r1 = 0; g1 = x; b1 = c; }
    else if (hp < 5) { r1 = x; g1 = 0; b1 = c; }
    else { r1 = c; g1 = 0; b1 = x; }
    double m = v - c;
    int r = static_cast<int>(std::lround((r1 + m) * 255));
    int gc = static_cast<int>(std::lround((g1 + m) * 255));
    int b = static_cast<int>(std::lround((b1 + m) * 255));
    r = std::clamp(r, 0, 255);
    gc = std::clamp(gc, 0, 255);
    b = std::clamp(b, 0, 255);
    return RGB(r, gc, b);
}

// ============================================================================
// Control "swatch": un rectangulo de color solido, usado para mostrar el
// color de un pixel, el color de pincel actual, y los colores de Reemplazar.
// ============================================================================

LRESULT CALLBACK SwatchWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            COLORREF c = static_cast<COLORREF>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
            HBRUSH brush = CreateSolidBrush(c);
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);
            FrameRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void SetSwatchColor(HWND hwnd, COLORREF c) {
    SetWindowLongPtr(hwnd, GWLP_USERDATA, static_cast<LONG_PTR>(c));
    InvalidateRect(hwnd, nullptr, TRUE);
}

// ============================================================================
// Rueda de color (HSV): un disco donde el angulo = matiz (hue) y la
// distancia al centro = saturacion. El brillo (value) se fija en 1.0 para
// la rueda; para colores mas oscuros se usa el selector nativo o los campos
// R/G/B manuales.
// ============================================================================

static const int WHEEL_SIZE = 150;
static const int WHEEL_RADIUS = 70;
static HBITMAP g_wheelBitmap = nullptr;
static POINT g_wheelMarker = {WHEEL_SIZE / 2, WHEEL_SIZE / 2};

void BuildWheelBitmap(HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    HDC memDC = CreateCompatibleDC(hdc);
    if (g_wheelBitmap) DeleteObject(g_wheelBitmap);
    g_wheelBitmap = CreateCompatibleBitmap(hdc, WHEEL_SIZE, WHEEL_SIZE);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, g_wheelBitmap));

    RECT rc = {0, 0, WHEEL_SIZE, WHEEL_SIZE};
    FillRect(memDC, &rc, static_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));

    int cx = WHEEL_SIZE / 2, cy = WHEEL_SIZE / 2;
    for (int y = 0; y < WHEEL_SIZE; ++y) {
        for (int x = 0; x < WHEEL_SIZE; ++x) {
            double dx = x - cx;
            double dy = y - cy;
            double r = std::sqrt(dx * dx + dy * dy);
            if (r <= WHEEL_RADIUS) {
                double hue = std::atan2(-dy, dx) * 180.0 / 3.14159265358979323846;
                if (hue < 0) hue += 360.0;
                double sat = r / WHEEL_RADIUS;
                if (sat > 1.0) sat = 1.0;
                SetPixel(memDC, x, y, HSVtoRGB(hue, sat, 1.0));
            }
        }
    }

    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    ReleaseDC(hwnd, hdc);
}

void PickWheelColor(HWND hwnd, int x, int y) {
    int cx = WHEEL_SIZE / 2, cy = WHEEL_SIZE / 2;
    double dx = x - cx, dy = y - cy;
    double r = std::sqrt(dx * dx + dy * dy);
    if (r > WHEEL_RADIUS) r = WHEEL_RADIUS;
    double hue = std::atan2(-dy, dx) * 180.0 / 3.14159265358979323846;
    if (hue < 0) hue += 360.0;
    double sat = r / WHEEL_RADIUS;
    COLORREF col = HSVtoRGB(hue, sat, 1.0);

    g_wheelMarker.x = x;
    g_wheelMarker.y = y;
    g.brushColor = col;
    SetSwatchColor(g_hSwatchBrush, col);

    wchar_t buf[16];
    swprintf(buf, 16, L"%d", GetRValue(col)); SetWindowTextW(g_hEditR, buf);
    swprintf(buf, 16, L"%d", GetGValue(col)); SetWindowTextW(g_hEditG, buf);
    swprintf(buf, 16, L"%d", GetBValue(col)); SetWindowTextW(g_hEditB, buf);
    InvalidateRect(hwnd, nullptr, TRUE);
}

LRESULT CALLBACK ColorWheelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            BuildWheelBitmap(hwnd);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, g_wheelBitmap));
            BitBlt(hdc, 0, 0, WHEEL_SIZE, WHEEL_SIZE, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteDC(memDC);

            HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
            HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
            Ellipse(hdc, g_wheelMarker.x - 5, g_wheelMarker.y - 5, g_wheelMarker.x + 5, g_wheelMarker.y + 5);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(pen);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            PickWheelColor(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEMOVE:
            if (wParam & MK_LBUTTON) PickWheelColor(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            ReleaseCapture();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            if (g_wheelBitmap) { DeleteObject(g_wheelBitmap); g_wheelBitmap = nullptr; }
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Lienzo (canvas): dibuja la imagen BMP cargada y traduce clics de mouse a
// coordenadas de pixel dentro de la imagen.
// ============================================================================

void GetCanvasImageRect(HWND hwnd, int imgW, int imgH, RECT& outRect, double& outScale) {
    RECT rc; GetClientRect(hwnd, &rc);
    int cw = rc.right - rc.left;
    int ch = rc.bottom - rc.top;
    double scaleX = static_cast<double>(cw) / imgW;
    double scaleY = static_cast<double>(ch) / imgH;
    double scale = std::min(scaleX, scaleY);
    if (scale <= 0) scale = 1.0;
    int drawW = static_cast<int>(imgW * scale);
    int drawH = static_cast<int>(imgH * scale);
    int offX = (cw - drawW) / 2;
    int offY = (ch - drawH) / 2;
    outRect = {offX, offY, offX + drawW, offY + drawH};
    outScale = scale;
}

bool CanvasPointToImage(HWND hwnd, int px, int py, int& imgX, int& imgY) {
    if (!g.image) return false;
    RECT rc; double scale;
    GetCanvasImageRect(hwnd, g.image->width(), g.image->height(), rc, scale);
    if (px < rc.left || px >= rc.right || py < rc.top || py >= rc.bottom) return false;
    imgX = static_cast<int>((px - rc.left) / scale);
    imgY = static_cast<int>((py - rc.top) / scale);
    imgX = std::clamp(imgX, 0, g.image->width() - 1);
    imgY = std::clamp(imgY, 0, g.image->height() - 1);
    return true;
}

void UpdateSelRectFields() {
    wchar_t buf[16];
    swprintf(buf, 16, L"%d", g.selRectX); SetWindowTextW(g_hEditSelX, buf);
    swprintf(buf, 16, L"%d", g.selRectY); SetWindowTextW(g_hEditSelY, buf);
    swprintf(buf, 16, L"%d", g.selRectW); SetWindowTextW(g_hEditSelW, buf);
    swprintf(buf, 16, L"%d", g.selRectH); SetWindowTextW(g_hEditSelH, buf);
}

void UpdatePixelPanel() {
    if (!g.image || g.selX < 0 || g.selY < 0) {
        SetWindowTextW(g_hEditR, L"");
        SetWindowTextW(g_hEditG, L"");
        SetWindowTextW(g_hEditB, L"");
        return;
    }
    Pixel p = g.image->get(g.selX, g.selY);
    wchar_t buf[16];
    swprintf(buf, 16, L"%d", p.r); SetWindowTextW(g_hEditR, buf);
    swprintf(buf, 16, L"%d", p.g); SetWindowTextW(g_hEditG, buf);
    swprintf(buf, 16, L"%d", p.b); SetWindowTextW(g_hEditB, buf);
    SetSwatchColor(g_hSwatchPixel, RGB(p.r, p.g, p.b));

    wchar_t status[128];
    swprintf(status, 128, L"Pixel seleccionado: (%d, %d)  RGB(%d,%d,%d)", g.selX, g.selY, p.r, p.g, p.b);
    SetStatus(status);
}

void PaintCanvas(HWND hwnd, HDC hdc) {
    RECT rc; GetClientRect(hwnd, &rc);
    HBRUSH bgBrush = CreateSolidBrush(RGB(40, 40, 40));
    FillRect(hdc, &rc, bgBrush);
    DeleteObject(bgBrush);

    if (!g.image) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(200, 200, 200));
        DrawTextW(hdc, L"Abre un archivo BMP con Archivo > Abrir...", -1, &rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    int w = g.image->width(), h = g.image->height();
    RECT imgRect; double scale;
    GetCanvasImageRect(hwnd, w, h, imgRect, scale);

    // Construir un DIB top-down en BGR con el padding de fila que exige GDI,
    // igual que exige el formato BMP en disco (ver bmp_image.cpp).
    int stride = ((w * 3 + 3) / 4) * 4;
    std::vector<uint8_t> dib(static_cast<size_t>(stride) * h, 0);
    const std::vector<uint8_t>& rgb = g.image->raw_rgb();
    for (int y = 0; y < h; ++y) {
        const uint8_t* srcRow = &rgb[static_cast<size_t>(y) * w * 3];
        uint8_t* dstRow = &dib[static_cast<size_t>(y) * stride];
        for (int x = 0; x < w; ++x) {
            dstRow[x * 3 + 0] = srcRow[x * 3 + 2];  // B
            dstRow[x * 3 + 1] = srcRow[x * 3 + 1];  // G
            dstRow[x * 3 + 2] = srcRow[x * 3 + 0];  // R
        }
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // negativo = top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchDIBits(hdc, imgRect.left, imgRect.top, imgRect.right - imgRect.left, imgRect.bottom - imgRect.top,
                  0, 0, w, h, dib.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);

    // Resaltar el pixel seleccionado (modo Seleccionar)
    if (g.mode == AppState::Mode::Select && g.selX >= 0 && g.selY >= 0) {
        int sx = imgRect.left + static_cast<int>(g.selX * scale);
        int sy = imgRect.top + static_cast<int>(g.selY * scale);
        int sw = std::max(1, static_cast<int>(std::ceil(scale)));
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 0));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
        Rectangle(hdc, sx - 2, sy - 2, sx + sw + 2, sy + sw + 2);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
    }

    // Rectangulo de seleccion (en progreso o confirmado) para Crop/Rect
    if ((g.mode == AppState::Mode::Crop || g.mode == AppState::Mode::Rect) && (g.dragging || g.hasSelectionRect)) {
        int rx0, ry0, rx1, ry1;
        if (g.dragging) {
            rx0 = std::min(g.dragStartX, g.dragCurX);
            ry0 = std::min(g.dragStartY, g.dragCurY);
            rx1 = std::max(g.dragStartX, g.dragCurX);
            ry1 = std::max(g.dragStartY, g.dragCurY);
        } else {
            rx0 = g.selRectX; ry0 = g.selRectY;
            rx1 = g.selRectX + g.selRectW; ry1 = g.selRectY + g.selRectH;
        }
        int sx0 = imgRect.left + static_cast<int>(rx0 * scale);
        int sy0 = imgRect.top + static_cast<int>(ry0 * scale);
        int sx1 = imgRect.left + static_cast<int>(rx1 * scale);
        int sy1 = imgRect.top + static_cast<int>(ry1 * scale);
        HPEN pen = CreatePen(PS_DASH, 1, RGB(0, 255, 0));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
        Rectangle(hdc, sx0, sy0, sx1, sy1);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
    }
}

LRESULT CALLBACK CanvasWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            PaintCanvas(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN: {
            if (!g.image) return 0;
            int ix, iy;
            if (!CanvasPointToImage(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), ix, iy)) return 0;
            if (g.mode == AppState::Mode::Select) {
                g.selX = ix; g.selY = iy;
                UpdatePixelPanel();
            } else {
                g.dragging = true;
                g.dragStartX = ix; g.dragStartY = iy;
                g.dragCurX = ix; g.dragCurY = iy;
                g.hasSelectionRect = false;
                SetCapture(hwnd);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g.dragging && g.image) {
                RECT rc; double scale;
                GetCanvasImageRect(hwnd, g.image->width(), g.image->height(), rc, scale);
                int ix = static_cast<int>((GET_X_LPARAM(lParam) - rc.left) / scale);
                int iy = static_cast<int>((GET_Y_LPARAM(lParam) - rc.top) / scale);
                g.dragCurX = std::clamp(ix, 0, g.image->width() - 1);
                g.dragCurY = std::clamp(iy, 0, g.image->height() - 1);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (g.dragging) {
                g.dragging = false;
                ReleaseCapture();
                g.selRectX = std::min(g.dragStartX, g.dragCurX);
                g.selRectY = std::min(g.dragStartY, g.dragCurY);
                g.selRectW = std::abs(g.dragCurX - g.dragStartX) + 1;
                g.selRectH = std::abs(g.dragCurY - g.dragStartY) + 1;
                g.hasSelectionRect = (g.selRectW > 0 && g.selRectH > 0);
                UpdateSelRectFields();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Acciones (funciones "Do*") invocadas desde los botones y el menu.
// ============================================================================

void DoOpenFile(HWND hwnd) {
    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Archivos BMP (*.bmp)\0*.bmp\0Todos los archivos\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"bmp";
    if (!GetOpenFileNameW(&ofn)) return;

    try {
        auto img = std::make_unique<BmpImage>(BmpImage::load(WideToUtf8(fileBuf)));
        g.image = std::move(img);
        g.original = std::make_unique<BmpImage>(*g.image);
        g.currentPath = fileBuf;
        g.unsavedChanges = false;
        g.selX = g.selY = -1;
        g.hasSelectionRect = false;
        g.dragging = false;

        UpdatePixelPanel();
        SetStatus(L"Archivo cargado: " + g.currentPath);
        SetWindowTextW(hwnd, (L"BmpEditor - " + g.currentPath).c_str());
        InvalidateRect(g_hCanvas, nullptr, TRUE);
    } catch (const BmpError& e) {
        MessageBoxW(hwnd, Utf8ToWide(e.what()).c_str(), L"Error al abrir el archivo", MB_ICONERROR);
    }
}

void DoSaveFile(HWND hwnd, bool saveAs) {
    if (!g.image) {
        MessageBoxW(hwnd, L"No hay ninguna imagen abierta.", L"BmpEditor", MB_ICONINFORMATION);
        return;
    }
    std::wstring path = g.currentPath;
    if (saveAs || path.empty()) {
        wchar_t fileBuf[MAX_PATH] = L"";
        if (!path.empty()) {
            size_t n = std::min(path.size(), static_cast<size_t>(MAX_PATH - 1));
            wcsncpy(fileBuf, path.c_str(), n);
            fileBuf[n] = 0;
        }
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"Archivos BMP (*.bmp)\0*.bmp\0";
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT;
        ofn.lpstrDefExt = L"bmp";
        if (!GetSaveFileNameW(&ofn)) return;
        path = fileBuf;
    }
    try {
        g.image->save(WideToUtf8(path));
        g.currentPath = path;
        g.unsavedChanges = false;
        SetStatus(L"Guardado en: " + path);
        SetWindowTextW(hwnd, (L"BmpEditor - " + path).c_str());
    } catch (const BmpError& e) {
        MessageBoxW(hwnd, Utf8ToWide(e.what()).c_str(), L"Error al guardar", MB_ICONERROR);
    }
}

void DoUndo() {
    if (!g.image || !g.original) return;
    *g.image = *g.original;
    g.unsavedChanges = false;
    g.selX = g.selY = -1;
    g.hasSelectionRect = false;
    UpdatePixelPanel();
    InvalidateRect(g_hCanvas, nullptr, TRUE);
    SetStatus(L"Todos los cambios fueron descartados.");
}

void DoSimpleOp(void (BmpImage::*fn)()) {
    if (!g.image) {
        MessageBoxW(g_hMainWnd, L"Abre una imagen primero.", L"BmpEditor", MB_ICONINFORMATION);
        return;
    }
    (g.image.get()->*fn)();
    g.unsavedChanges = true;
    g.selX = g.selY = -1;
    g.hasSelectionRect = false;
    UpdatePixelPanel();
    InvalidateRect(g_hCanvas, nullptr, TRUE);
    SetStatus(L"Operacion aplicada.");
}

void DoApplySelection(HWND hwnd) {
    if (!g.image) return;
    if (!g.hasSelectionRect) {
        MessageBoxW(hwnd, L"Primero arrastra un area en la imagen (modo Recortar o Rectangulo).",
                    L"BmpEditor", MB_ICONINFORMATION);
        return;
    }
    try {
        if (g.mode == AppState::Mode::Crop) {
            g.image->crop(g.selRectX, g.selRectY, g.selRectW, g.selRectH);
            SetStatus(L"Recorte aplicado.");
        } else if (g.mode == AppState::Mode::Rect) {
            bool filled = (SendMessage(g_hChkFill, BM_GETCHECK, 0, 0) == BST_CHECKED);
            Pixel color{GetRValue(g.brushColor), GetGValue(g.brushColor), GetBValue(g.brushColor)};
            g.image->draw_rectangle(g.selRectX, g.selRectY, g.selRectW, g.selRectH, color, filled);
            SetStatus(L"Rectangulo dibujado.");
        } else {
            return;
        }
        g.unsavedChanges = true;
        g.hasSelectionRect = false;
        g.selX = g.selY = -1;
        UpdatePixelPanel();
        InvalidateRect(g_hCanvas, nullptr, TRUE);
    } catch (const BmpError& e) {
        MessageBoxW(hwnd, Utf8ToWide(e.what()).c_str(), L"Error", MB_ICONERROR);
    }
}

void DoApplyResize(HWND hwnd) {
    if (!g.image) return;
    wchar_t buf[16];
    GetWindowTextW(g_hEditResizeW, buf, 16);
    int w = _wtoi(buf);
    GetWindowTextW(g_hEditResizeH, buf, 16);
    int h = _wtoi(buf);
    if (w <= 0 || h <= 0) {
        MessageBoxW(hwnd, L"Ingresa un ancho y alto validos (mayores a 0).", L"BmpEditor", MB_ICONWARNING);
        return;
    }
    try {
        g.image->resize_nearest(w, h);
        g.unsavedChanges = true;
        g.selX = g.selY = -1;
        g.hasSelectionRect = false;
        UpdatePixelPanel();
        InvalidateRect(g_hCanvas, nullptr, TRUE);
        SetStatus(L"Imagen redimensionada.");
    } catch (const BmpError& e) {
        MessageBoxW(hwnd, Utf8ToWide(e.what()).c_str(), L"Error", MB_ICONERROR);
    }
}

void DoApplyBrightness() {
    if (!g.image) return;
    int delta = static_cast<int>(SendMessage(g_hTrackBrightness, TBM_GETPOS, 0, 0));
    g.image->adjust_brightness(delta);
    g.unsavedChanges = true;
    InvalidateRect(g_hCanvas, nullptr, TRUE);
    SetStatus(L"Brillo ajustado.");
}

void DoApplyContrast() {
    if (!g.image) return;
    int pos = static_cast<int>(SendMessage(g_hTrackContrast, TBM_GETPOS, 0, 0));
    double factor = pos / 100.0;
    g.image->adjust_contrast(factor);
    g.unsavedChanges = true;
    InvalidateRect(g_hCanvas, nullptr, TRUE);
    SetStatus(L"Contraste ajustado.");
}

void DoApplyPixelColorFromEdits(HWND hwnd) {
    if (!g.image || g.selX < 0 || g.selY < 0) {
        MessageBoxW(hwnd, L"Primero selecciona un pixel en modo Seleccionar (clic en la imagen).",
                    L"BmpEditor", MB_ICONINFORMATION);
        return;
    }
    wchar_t buf[16];
    GetWindowTextW(g_hEditR, buf, 16); int r = std::clamp(_wtoi(buf), 0, 255);
    GetWindowTextW(g_hEditG, buf, 16); int gc = std::clamp(_wtoi(buf), 0, 255);
    GetWindowTextW(g_hEditB, buf, 16); int b = std::clamp(_wtoi(buf), 0, 255);
    g.image->set(g.selX, g.selY, Pixel{static_cast<uint8_t>(r), static_cast<uint8_t>(gc), static_cast<uint8_t>(b)});
    g.unsavedChanges = true;
    UpdatePixelPanel();
    InvalidateRect(g_hCanvas, nullptr, TRUE);
    SetStatus(L"Color del pixel actualizado.");
}

void DoChooseColorForPixel(HWND hwnd) {
    if (!g.image || g.selX < 0 || g.selY < 0) {
        MessageBoxW(hwnd, L"Primero selecciona un pixel en modo Seleccionar (clic en la imagen).",
                    L"BmpEditor", MB_ICONINFORMATION);
        return;
    }
    Pixel p = g.image->get(g.selX, g.selY);
    static COLORREF customColors[16] = {0};
    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd;
    cc.lpCustColors = customColors;
    cc.rgbResult = RGB(p.r, p.g, p.b);
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc)) {
        Pixel newP{GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult)};
        g.image->set(g.selX, g.selY, newP);
        g.unsavedChanges = true;
        UpdatePixelPanel();
        InvalidateRect(g_hCanvas, nullptr, TRUE);
        SetStatus(L"Color del pixel actualizado mediante el selector de color.");
    }
}

void DoPickColor(HWND hwnd, COLORREF& target, HWND swatch) {
    static COLORREF customColors[16] = {0};
    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd;
    cc.lpCustColors = customColors;
    cc.rgbResult = target;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc)) {
        target = cc.rgbResult;
        SetSwatchColor(swatch, target);
    }
}

void DoReplace() {
    if (!g.image) return;
    wchar_t buf[16];
    GetWindowTextW(g_hEditTolerance, buf, 16);
    int tol = std::max(0, _wtoi(buf));
    Pixel from{GetRValue(g.srcColor), GetGValue(g.srcColor), GetBValue(g.srcColor)};
    Pixel to{GetRValue(g.dstColor), GetGValue(g.dstColor), GetBValue(g.dstColor)};
    int changed = g.image->replace_color(from, to, tol);
    g.unsavedChanges = g.unsavedChanges || (changed > 0);
    InvalidateRect(g_hCanvas, nullptr, TRUE);
    wchar_t msg[64];
    swprintf(msg, 64, L"%d pixel(es) reemplazados.", changed);
    SetStatus(msg);
}

// ============================================================================
// Construccion del menu y de los controles de la ventana principal
// ============================================================================

HMENU CreateMainMenu() {
    HMENU hMenuBar = CreateMenu();

    HMENU hFileMenu = CreatePopupMenu();
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_OPEN, L"&Abrir...\tCtrl+O");
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_SAVE, L"&Guardar\tCtrl+S");
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_SAVEAS, L"Guardar &como...");
    AppendMenuW(hFileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_EXIT, L"&Salir");
    AppendMenuW(hMenuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hFileMenu), L"&Archivo");

    HMENU hEditMenu = CreatePopupMenu();
    AppendMenuW(hEditMenu, MF_STRING, ID_EDIT_UNDO, L"&Deshacer todo\tCtrl+Z");
    AppendMenuW(hMenuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hEditMenu), L"&Edicion");

    return hMenuBar;
}

HWND MakeLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr, g_hInstance, nullptr);
}
HWND MakeButton(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y, w, h, parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInstance, nullptr);
}
HWND MakeEdit(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y, w, h, parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInstance, nullptr);
}
HWND MakeCheckbox(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, x, y, w, h,
                            parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInstance, nullptr);
}
HWND MakeGroupBox(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, x, y, w, h, parent, nullptr,
                            g_hInstance, nullptr);
}
HWND MakeTrackbar(HWND parent, int id, int x, int y, int w, int h, int minV, int maxV, int pos) {
    HWND tb = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS, x, y, w, h,
                               parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInstance, nullptr);
    SendMessage(tb, TBM_SETRANGE, TRUE, MAKELPARAM(minV, maxV));
    SendMessage(tb, TBM_SETPOS, TRUE, pos);
    return tb;
}
HWND MakeSwatch(HWND parent, int id, int x, int y, int w, int h, COLORREF initial) {
    HWND s = CreateWindowExW(WS_EX_CLIENTEDGE, L"BmpSwatchClass", L"", WS_CHILD | WS_VISIBLE, x, y, w, h, parent,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInstance, nullptr);
    SetSwatchColor(s, initial);
    return s;
}

void CreateControls(HWND hwnd) {
    // --- Barra superior de operaciones rapidas ---
    int tx = 10, ty = 8, bw = 140, bh = 28, gap = 8;
    MakeButton(hwnd, L"Escala de grises", ID_BTN_GRAYSCALE, tx, ty, bw, bh); tx += bw + gap;
    MakeButton(hwnd, L"Invertir", ID_BTN_INVERT, tx, ty, bw, bh); tx += bw + gap;
    MakeButton(hwnd, L"Flip H", ID_BTN_FLIPH, tx, ty, bw, bh); tx += bw + gap;
    MakeButton(hwnd, L"Flip V", ID_BTN_FLIPV, tx, ty, bw, bh); tx += bw + gap;
    MakeButton(hwnd, L"Rotar 90", ID_BTN_ROTATE90, tx, ty, bw, bh); tx += bw + gap;
    MakeButton(hwnd, L"Rotar 180", ID_BTN_ROTATE180, tx, ty, bw, bh);

    // --- Lienzo ---
    g_hCanvas = CreateWindowExW(WS_EX_CLIENTEDGE, L"BmpCanvasClass", L"", WS_CHILD | WS_VISIBLE, 10, 46, 780, 800,
                                 hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CANVAS)), g_hInstance, nullptr);

    // --- Barra de estado ---
    g_hStatus = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"Listo.", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                                 10, 856, 1160, 26, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)),
                                 g_hInstance, nullptr);

    // --- Panel derecho ---
    const int px = 800, pw = 360;

    // Grupo: Pixel seleccionado
    MakeGroupBox(hwnd, L"Pixel seleccionado", px, 46, pw, 110);
    MakeLabel(hwnd, L"R:", px + 8, 80, 18, 20);
    g_hEditR = MakeEdit(hwnd, ID_EDIT_PIXEL_R, px + 26, 78, 40, 22);
    MakeLabel(hwnd, L"G:", px + 72, 80, 18, 20);
    g_hEditG = MakeEdit(hwnd, ID_EDIT_PIXEL_G, px + 90, 78, 40, 22);
    MakeLabel(hwnd, L"B:", px + 136, 80, 18, 20);
    g_hEditB = MakeEdit(hwnd, ID_EDIT_PIXEL_B, px + 154, 78, 40, 22);
    g_hSwatchPixel = MakeSwatch(hwnd, ID_SWATCH_PIXEL, px + 200, 78, 36, 22, RGB(255, 255, 255));
    MakeButton(hwnd, L"Elegir color...", ID_BTN_CHOOSE_COLOR_DIALOG, px + 244, 78, 108, 24);
    MakeButton(hwnd, L"Aplicar al pixel (R/G/B de arriba)", ID_BTN_APPLY_PIXEL_COLOR, px + 8, 106, 340, 22);

    // Grupo: Rueda de color
    MakeGroupBox(hwnd, L"Rueda de color (pincel para Rectangulo)", px, 162, pw, 170);
    g_hColorWheel = CreateWindowExW(WS_EX_CLIENTEDGE, L"BmpColorWheelClass", L"", WS_CHILD | WS_VISIBLE, px + 8, 178,
                                     WHEEL_SIZE, WHEEL_SIZE, hwnd,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_COLORWHEEL)), g_hInstance, nullptr);
    g_hSwatchBrush = MakeSwatch(hwnd, ID_SWATCH_BRUSH, px + 170, 178, 50, 50, g.brushColor);
    MakeLabel(hwnd, L"Clic o arrastra en la rueda\npara elegir matiz/saturacion.\nEste color se usa al\ndibujar un Rectangulo.",
              px + 170, 234, 180, 90);

    // Grupo: Herramientas de seleccion (Recortar / Rectangulo)
    MakeGroupBox(hwnd, L"Herramientas de seleccion en el lienzo", px, 340, pw, 150);
    MakeButton(hwnd, L"Seleccionar", ID_BTN_MODE_SELECT, px + 8, 358, 108, 24);
    MakeButton(hwnd, L"Recortar", ID_BTN_MODE_CROP, px + 124, 358, 108, 24);
    MakeButton(hwnd, L"Rectangulo", ID_BTN_MODE_RECT, px + 240, 358, 108, 24);
    MakeLabel(hwnd, L"Arrastra el mouse en la imagen para definir un area:", px + 8, 388, 340, 16);
    MakeLabel(hwnd, L"X:", px + 8, 408, 16, 18);
    g_hEditSelX = MakeEdit(hwnd, ID_EDIT_SEL_X, px + 26, 406, 44, 20);
    MakeLabel(hwnd, L"Y:", px + 74, 408, 16, 18);
    g_hEditSelY = MakeEdit(hwnd, ID_EDIT_SEL_Y, px + 92, 406, 44, 20);
    MakeLabel(hwnd, L"W:", px + 140, 408, 18, 18);
    g_hEditSelW = MakeEdit(hwnd, ID_EDIT_SEL_W, px + 160, 406, 44, 20);
    MakeLabel(hwnd, L"H:", px + 208, 408, 16, 18);
    g_hEditSelH = MakeEdit(hwnd, ID_EDIT_SEL_H, px + 226, 406, 44, 20);
    g_hChkFill = MakeCheckbox(hwnd, L"Relleno (solo Rectangulo)", ID_CHK_FILL, px + 8, 432, 250, 20);
    MakeButton(hwnd, L"Aplicar (recortar / dibujar rectangulo)", ID_BTN_APPLY_SELECTION, px + 8, 458, 340, 26);

    // Grupo: Redimensionar
    MakeGroupBox(hwnd, L"Redimensionar imagen", px, 498, pw, 72);
    MakeLabel(hwnd, L"Ancho:", px + 8, 528, 46, 18);
    g_hEditResizeW = MakeEdit(hwnd, ID_EDIT_RESIZE_W, px + 58, 526, 56, 20);
    MakeLabel(hwnd, L"Alto:", px + 130, 528, 40, 18);
    g_hEditResizeH = MakeEdit(hwnd, ID_EDIT_RESIZE_H, px + 174, 526, 56, 20);
    MakeButton(hwnd, L"Aplicar", ID_BTN_APPLY_RESIZE, px + 244, 524, 108, 24);

    // Grupo: Brillo / Contraste
    MakeGroupBox(hwnd, L"Brillo y contraste", px, 578, pw, 130);
    g_hLabelBrightness = MakeLabel(hwnd, L"Brillo: 0", px + 8, 596, 150, 16);
    g_hTrackBrightness = MakeTrackbar(hwnd, ID_TRACKBAR_BRIGHTNESS, px + 8, 612, 250, 28, -255, 255, 0);
    MakeButton(hwnd, L"Aplicar", ID_BTN_APPLY_BRIGHTNESS, px + 266, 612, 84, 26);
    g_hLabelContrast = MakeLabel(hwnd, L"Contraste: x1.00", px + 8, 646, 150, 16);
    g_hTrackContrast = MakeTrackbar(hwnd, ID_TRACKBAR_CONTRAST, px + 8, 662, 250, 28, 0, 300, 100);
    MakeButton(hwnd, L"Aplicar", ID_BTN_APPLY_CONTRAST, px + 266, 662, 84, 26);

    // Grupo: Reemplazar color
    MakeGroupBox(hwnd, L"Reemplazar un color por otro", px, 716, pw, 130);
    MakeLabel(hwnd, L"Origen:", px + 8, 734, 56, 18);
    g_hSwatchSrc = MakeSwatch(hwnd, ID_SWATCH_SRC, px + 68, 732, 36, 24, g.srcColor);
    MakeButton(hwnd, L"Elegir...", ID_BTN_PICK_SRC_COLOR, px + 112, 732, 90, 24);
    MakeLabel(hwnd, L"Destino:", px + 8, 762, 56, 18);
    g_hSwatchDst = MakeSwatch(hwnd, ID_SWATCH_DST, px + 68, 760, 36, 24, g.dstColor);
    MakeButton(hwnd, L"Elegir...", ID_BTN_PICK_DST_COLOR, px + 112, 760, 90, 24);
    MakeLabel(hwnd, L"Tolerancia:", px + 8, 790, 66, 18);
    g_hEditTolerance = MakeEdit(hwnd, ID_EDIT_TOLERANCE, px + 76, 788, 50, 20);
    SetWindowTextW(g_hEditTolerance, L"0");
    MakeButton(hwnd, L"Reemplazar en toda la imagen", ID_BTN_REPLACE, px + 8, 816, 340, 26);
}

// ============================================================================
// Procedimiento de la ventana principal
// ============================================================================

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            CreateControls(hwnd);
            return 0;

        case WM_HSCROLL: {
            HWND ctrl = reinterpret_cast<HWND>(lParam);
            if (ctrl == g_hTrackBrightness) {
                int pos = static_cast<int>(SendMessage(g_hTrackBrightness, TBM_GETPOS, 0, 0));
                wchar_t buf[32]; swprintf(buf, 32, L"Brillo: %d", pos);
                SetWindowTextW(g_hLabelBrightness, buf);
            } else if (ctrl == g_hTrackContrast) {
                int pos = static_cast<int>(SendMessage(g_hTrackContrast, TBM_GETPOS, 0, 0));
                wchar_t buf[48]; swprintf(buf, 48, L"Contraste: x%.2f", pos / 100.0);
                SetWindowTextW(g_hLabelContrast, buf);
            }
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_FILE_OPEN: DoOpenFile(hwnd); break;
                case ID_FILE_SAVE: DoSaveFile(hwnd, false); break;
                case ID_FILE_SAVEAS: DoSaveFile(hwnd, true); break;
                case ID_FILE_EXIT: PostMessage(hwnd, WM_CLOSE, 0, 0); break;
                case ID_EDIT_UNDO: DoUndo(); break;

                case ID_BTN_GRAYSCALE: DoSimpleOp(&BmpImage::to_grayscale); break;
                case ID_BTN_INVERT: DoSimpleOp(&BmpImage::invert); break;
                case ID_BTN_FLIPH: DoSimpleOp(&BmpImage::flip_horizontal); break;
                case ID_BTN_FLIPV: DoSimpleOp(&BmpImage::flip_vertical); break;
                case ID_BTN_ROTATE90: DoSimpleOp(&BmpImage::rotate90_cw); break;
                case ID_BTN_ROTATE180: DoSimpleOp(&BmpImage::rotate180); break;

                case ID_BTN_MODE_SELECT:
                    g.mode = AppState::Mode::Select; g.dragging = false; g.hasSelectionRect = false;
                    SetStatus(L"Modo: Seleccionar pixel (clic en la imagen).");
                    InvalidateRect(g_hCanvas, nullptr, TRUE);
                    break;
                case ID_BTN_MODE_CROP:
                    g.mode = AppState::Mode::Crop; g.dragging = false; g.hasSelectionRect = false;
                    SetStatus(L"Modo: Recortar (arrastra un area y presiona Aplicar).");
                    InvalidateRect(g_hCanvas, nullptr, TRUE);
                    break;
                case ID_BTN_MODE_RECT:
                    g.mode = AppState::Mode::Rect; g.dragging = false; g.hasSelectionRect = false;
                    SetStatus(L"Modo: Dibujar rectangulo (arrastra un area y presiona Aplicar).");
                    InvalidateRect(g_hCanvas, nullptr, TRUE);
                    break;
                case ID_BTN_APPLY_SELECTION: DoApplySelection(hwnd); break;

                case ID_BTN_APPLY_RESIZE: DoApplyResize(hwnd); break;
                case ID_BTN_APPLY_BRIGHTNESS: DoApplyBrightness(); break;
                case ID_BTN_APPLY_CONTRAST: DoApplyContrast(); break;

                case ID_BTN_APPLY_PIXEL_COLOR: DoApplyPixelColorFromEdits(hwnd); break;
                case ID_BTN_CHOOSE_COLOR_DIALOG: DoChooseColorForPixel(hwnd); break;

                case ID_BTN_PICK_SRC_COLOR: DoPickColor(hwnd, g.srcColor, g_hSwatchSrc); break;
                case ID_BTN_PICK_DST_COLOR: DoPickColor(hwnd, g.dstColor, g_hSwatchDst); break;
                case ID_BTN_REPLACE: DoReplace(); break;
            }
            return 0;
        }

        case WM_CLOSE: {
            if (g.unsavedChanges) {
                int res = MessageBoxW(hwnd, L"Tienes cambios sin guardar. Deseas guardarlos antes de salir?",
                                       L"BmpEditor", MB_YESNOCANCEL | MB_ICONWARNING);
                if (res == IDCANCEL) return 0;
                if (res == IDYES) {
                    DoSaveFile(hwnd, false);
                    if (g.unsavedChanges) return 0;  // el guardado fallo o se cancelo
                }
            }
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Punto de entrada
// ============================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    g_hInstance = hInstance;

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wcMain = {};
    wcMain.cbSize = sizeof(wcMain);
    wcMain.lpfnWndProc = MainWndProc;
    wcMain.hInstance = hInstance;
    wcMain.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcMain.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wcMain.lpszClassName = L"BmpEditorMainClass";
    RegisterClassExW(&wcMain);

    WNDCLASSEXW wcCanvas = {};
    wcCanvas.cbSize = sizeof(wcCanvas);
    wcCanvas.lpfnWndProc = CanvasWndProc;
    wcCanvas.hInstance = hInstance;
    wcCanvas.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wcCanvas.lpszClassName = L"BmpCanvasClass";
    RegisterClassExW(&wcCanvas);

    WNDCLASSEXW wcWheel = {};
    wcWheel.cbSize = sizeof(wcWheel);
    wcWheel.lpfnWndProc = ColorWheelWndProc;
    wcWheel.hInstance = hInstance;
    wcWheel.hCursor = LoadCursor(nullptr, IDC_HAND);
    wcWheel.lpszClassName = L"BmpColorWheelClass";
    RegisterClassExW(&wcWheel);

    WNDCLASSEXW wcSwatch = {};
    wcSwatch.cbSize = sizeof(wcSwatch);
    wcSwatch.lpfnWndProc = SwatchWndProc;
    wcSwatch.hInstance = hInstance;
    wcSwatch.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcSwatch.lpszClassName = L"BmpSwatchClass";
    RegisterClassExW(&wcSwatch);

    RECT wr = {0, 0, MAIN_WIDTH, MAIN_HEIGHT};
    DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&wr, style, TRUE);  // TRUE: la ventana tiene menu

    HWND hwnd = CreateWindowExW(0, L"BmpEditorMainClass", L"BmpEditor - Editor visual de imagenes BMP", style,
                                 CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, nullptr,
                                 CreateMainMenu(), hInstance, nullptr);
    if (!hwnd) return 0;
    g_hMainWnd = hwnd;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}
