// gui_main.cpp
// BmpEditor GUI - Aplicacion de escritorio nativa para Windows (Win32 API + GDI).
//
// Reutiliza directamente la clase bmpeditor::BmpImage (la misma biblioteca
// que usa el CLI bmpedit) para toda la logica de bajo nivel del formato BMP
// y las operaciones de edicion. Este archivo SOLO se encarga de la interfaz
// grafica: ventana principal, lienzo donde se ve y se hace clic en la
// imagen, una rueda de color HSV para elegir el color a aplicar, y botones
// para cada operacion.
//
// Requiere Windows + Visual Studio (usa <windows.h>, <commctrl.h>,
// GetOpenFileNameW/GetSaveFileNameW). No compila en Linux/macOS a proposito
// (se incluye en CMakeLists.txt dentro de un bloque if(WIN32)).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>  // _wtoi, _wtof
#include <cwchar>   // swprintf
#include <string>
#include <vector>

#include "bmp_image.hpp"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

// Habilita los controles comunes con estilo visual moderno (necesario para
// que el trackbar / botones se vean bien en Windows 10/11).
#pragma comment(linker, \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' "\
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using bmpeditor::BmpError;
using bmpeditor::BmpImage;
using bmpeditor::Pixel;

// ============================================================================
//  Identificadores de controles
// ============================================================================

enum ControlIds {
    IDC_CANVAS = 1001,
    IDC_WHEEL,
    IDC_SWATCH,
    IDC_VALUE_SLIDER,
    IDC_LABEL_SELECTED,
    IDC_LABEL_STATUS,

    IDC_EDIT_R, IDC_EDIT_G, IDC_EDIT_B,
    IDC_BTN_SET_RGB,
    IDC_BTN_APPLY_PIXEL,

    IDC_BTN_GRAYSCALE, IDC_BTN_INVERT, IDC_BTN_FLIPH, IDC_BTN_FLIPV,
    IDC_BTN_ROTATE90, IDC_BTN_ROTATE180,

    IDC_EDIT_BRIGHTNESS, IDC_BTN_BRIGHTNESS,
    IDC_EDIT_CONTRAST, IDC_BTN_CONTRAST,

    IDC_EDIT_CROP_X, IDC_EDIT_CROP_Y, IDC_EDIT_CROP_W, IDC_EDIT_CROP_H, IDC_BTN_CROP,
    IDC_EDIT_RESIZE_W, IDC_EDIT_RESIZE_H, IDC_BTN_RESIZE,

    IDC_EDIT_RECT_X, IDC_EDIT_RECT_Y, IDC_EDIT_RECT_W, IDC_EDIT_RECT_H,
    IDC_CHECK_FILL, IDC_BTN_RECT,

    IDC_EDIT_TOLERANCE, IDC_BTN_REPLACE,

    IDC_BTN_UNDO,

    ID_FILE_OPEN = 2001,
    ID_FILE_SAVE,
    ID_FILE_SAVEAS,
    ID_FILE_EXIT,
};

// ============================================================================
//  Estado global de la aplicacion
// ============================================================================
//
// Para una aplicacion Win32 pequena como esta (un unico documento abierto a
// la vez) es una practica comun y clara mantener el estado en una unica
// estructura global en vez de pasarla manualmente por cada callback.

struct AppState {
    BmpImage img;                 // imagen actualmente cargada/editada
    BmpImage original;            // copia tomada al abrir, para "Deshacer"
    bool hasImage = false;
    bool unsavedChanges = false;
    std::wstring currentPath;     // ruta del archivo actualmente abierto

    bool hasSelection = false;
    int selX = 0, selY = 0;

    // Datos de como se dibuja la imagen dentro del lienzo (se recalculan en
    // cada WM_PAINT del lienzo y se usan luego para traducir clics de mouse
    // a coordenadas de pixel de la imagen).
    double canvasScale = 1.0;
    int canvasOffsetX = 0, canvasOffsetY = 0;

    // Color actualmente seleccionado en la rueda / cuadros RGB.
    Pixel currentColor{255, 0, 0};
    double wheelHue = 0.0;    // grados, 0-360
    double wheelSat = 1.0;    // 0-1
    double wheelValue = 1.0;  // 0-1 (controlado por el slider vertical)
    bool hasWheelMarker = false;
    int wheelMarkerX = 0, wheelMarkerY = 0;  // coords cliente dentro de la rueda

    HBITMAP hWheelBitmap = nullptr;  // rueda HSV precomputada (se dibuja una sola vez)

    // Handles de los controles (se llenan en WM_CREATE de la ventana principal).
    HWND hMainWnd = nullptr;
    HWND hCanvas = nullptr;
    HWND hWheel = nullptr;
    HWND hSwatch = nullptr;
    HWND hValueSlider = nullptr;
    HWND hLabelSelected = nullptr;
    HWND hLabelStatus = nullptr;
    HWND hEditR = nullptr, hEditG = nullptr, hEditB = nullptr;
    HWND hEditBrightness = nullptr, hEditContrast = nullptr;
    HWND hEditCropX = nullptr, hEditCropY = nullptr, hEditCropW = nullptr, hEditCropH = nullptr;
    HWND hEditResizeW = nullptr, hEditResizeH = nullptr;
    HWND hEditRectX = nullptr, hEditRectY = nullptr, hEditRectW = nullptr, hEditRectH = nullptr;
    HWND hCheckFill = nullptr;
    HWND hEditTolerance = nullptr;
};

static AppState g_state;

// ============================================================================
//  Utilidades de color (HSV <-> RGB) para la rueda de color
// ============================================================================

static Pixel HsvToRgb(double h, double s, double v) {
    // h en [0,360), s y v en [0,1].
    double c = v * s;
    double hh = h / 60.0;
    double x = c * (1.0 - std::fabs(std::fmod(hh, 2.0) - 1.0));
    double r = 0, g = 0, b = 0;

    if (hh < 1)      { r = c; g = x; b = 0; }
    else if (hh < 2) { r = x; g = c; b = 0; }
    else if (hh < 3) { r = 0; g = c; b = x; }
    else if (hh < 4) { r = 0; g = x; b = c; }
    else if (hh < 5) { r = x; g = 0; b = c; }
    else             { r = c; g = 0; b = x; }

    double m = v - c;
    auto to_byte = [](double val) {
        int v255 = static_cast<int>(std::lround(val * 255.0));
        if (v255 < 0) v255 = 0;
        if (v255 > 255) v255 = 255;
        return static_cast<uint8_t>(v255);
    };
    return Pixel{to_byte(r + m), to_byte(g + m), to_byte(b + m)};
}

// ============================================================================
//  Utilidades de UI
// ============================================================================

static HWND MakeStatic(HWND parent, HINSTANCE hInst, const wchar_t* text,
                        int x, int y, int w, int h) {
    return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                          x, y, w, h, parent, nullptr, hInst, nullptr);
}

static HWND MakeButton(HWND parent, HINSTANCE hInst, const wchar_t* text,
                        int x, int y, int w, int h, int id) {
    return CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                          x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                          hInst, nullptr);
}

static HWND MakeEdit(HWND parent, HINSTANCE hInst, const wchar_t* text,
                      int x, int y, int w, int h, int id) {
    return CreateWindowW(L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                          x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                          hInst, nullptr);
}

static HWND MakeCheckbox(HWND parent, HINSTANCE hInst, const wchar_t* text,
                          int x, int y, int w, int h, int id) {
    return CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                          x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                          hInst, nullptr);
}

static int GetEditInt(HWND hEdit, int fallback = 0) {
    wchar_t buf[64];
    GetWindowTextW(hEdit, buf, 64);
    if (buf[0] == L'\0') return fallback;
    return _wtoi(buf);
}

static double GetEditDouble(HWND hEdit, double fallback = 0.0) {
    wchar_t buf[64];
    GetWindowTextW(hEdit, buf, 64);
    if (buf[0] == L'\0') return fallback;
    return _wtof(buf);
}

static void SetEditInt(HWND hEdit, int value) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%d", value);
    SetWindowTextW(hEdit, buf);
}

// Refleja g_state.currentColor en los 3 cuadros de texto R, G, B.
static void UpdateRgbEditsFromCurrentColor() {
    SetEditInt(g_state.hEditR, g_state.currentColor.r);
    SetEditInt(g_state.hEditG, g_state.currentColor.g);
    SetEditInt(g_state.hEditB, g_state.currentColor.b);
    InvalidateRect(g_state.hSwatch, nullptr, TRUE);
}

static void UpdateStatusLabel() {
    std::wstring text = g_state.unsavedChanges ? L"Cambios sin guardar" : L"Todo guardado";
    if (!g_state.hasImage) text = L"Ningun archivo abierto";
    SetWindowTextW(g_state.hLabelStatus, text.c_str());
}

static void UpdateSelectedLabel() {
    if (!g_state.hasSelection) {
        SetWindowTextW(g_state.hLabelSelected, L"Pixel seleccionado: (ninguno)");
        return;
    }
    Pixel p = g_state.img.get(g_state.selX, g_state.selY);
    wchar_t buf[128];
    swprintf(buf, 128, L"Pixel (%d, %d) = RGB(%d, %d, %d)",
             g_state.selX, g_state.selY, p.r, p.g, p.b);
    SetWindowTextW(g_state.hLabelSelected, buf);
}

static void RefreshWindowTitle() {
    std::wstring title = L"BmpEditor";
    if (g_state.hasImage) {
        title += L" - ";
        title += g_state.currentPath.empty() ? L"(sin nombre)" : g_state.currentPath;
        if (g_state.unsavedChanges) title += L" *";
    }
    SetWindowTextW(g_state.hMainWnd, title.c_str());
}

static void RefreshAll() {
    InvalidateRect(g_state.hCanvas, nullptr, TRUE);
    UpdateSelectedLabel();
    UpdateStatusLabel();
    RefreshWindowTitle();
}

// Construye el buffer de pixeles en el formato que Windows espera para
// StretchDIBits: filas de abajo hacia arriba, canales B-G-R, con padding de
// fila a multiplos de 4 bytes — exactamente el mismo formato binario que
// usa el propio archivo .bmp en disco (ver docs/FORMATO_BMP.md).
static std::vector<uint8_t> BuildDibBuffer(const BmpImage& img, int& strideOut) {
    int w = img.width();
    int h = img.height();
    int stride = ((w * 3 + 3) / 4) * 4;
    std::vector<uint8_t> buf(static_cast<size_t>(stride) * h, 0);

    const auto& rgb = img.raw_rgb();  // top-down, RGB, sin padding
    for (int bufRow = 0; bufRow < h; ++bufRow) {
        int imgRow = h - 1 - bufRow;  // bufRow 0 = fila inferior de la imagen
        const uint8_t* srcRow = &rgb[static_cast<size_t>(imgRow) * w * 3];
        uint8_t* dstRow = &buf[static_cast<size_t>(bufRow) * stride];
        for (int x = 0; x < w; ++x) {
            dstRow[x * 3 + 0] = srcRow[x * 3 + 2];  // B
            dstRow[x * 3 + 1] = srcRow[x * 3 + 1];  // G
            dstRow[x * 3 + 2] = srcRow[x * 3 + 0];  // R
        }
    }
    strideOut = stride;
    return buf;
}

// ============================================================================
//  Ventana de lienzo (dibuja la imagen y captura clics de pixel)
// ============================================================================

static LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));

            if (g_state.hasImage) {
                int w = g_state.img.width();
                int h = g_state.img.height();
                int stride = 0;
                std::vector<uint8_t> buf = BuildDibBuffer(g_state.img, stride);

                BITMAPINFO bmi = {};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = w;
                bmi.bmiHeader.biHeight = h;  // positivo: filas de abajo hacia arriba
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 24;
                bmi.bmiHeader.biCompression = BI_RGB;

                int destW = rc.right - rc.left;
                int destH = rc.bottom - rc.top;
                double scale = (destW > 0 && destH > 0)
                                   ? (std::min)(static_cast<double>(destW) / w,
                                                static_cast<double>(destH) / h)
                                   : 1.0;
                int dw = (std::max)(1, static_cast<int>(w * scale));
                int dh = (std::max)(1, static_cast<int>(h * scale));

                g_state.canvasScale = scale;
                g_state.canvasOffsetX = (destW - dw) / 2;
                g_state.canvasOffsetY = (destH - dh) / 2;

                SetStretchBltMode(hdc, COLORONCOLOR);
                StretchDIBits(hdc, g_state.canvasOffsetX, g_state.canvasOffsetY, dw, dh,
                              0, 0, w, h, buf.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);

                if (g_state.hasSelection) {
                    int sx = g_state.canvasOffsetX + static_cast<int>(g_state.selX * scale);
                    int sy = g_state.canvasOffsetY + static_cast<int>(g_state.selY * scale);
                    int ssize = (std::max)(2, static_cast<int>(scale));

                    HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 0));
                    HGDIOBJ oldPen = SelectObject(hdc, pen);
                    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    Rectangle(hdc, sx - 1, sy - 1, sx + ssize + 1, sy + ssize + 1);
                    SelectObject(hdc, oldPen);
                    SelectObject(hdc, oldBrush);
                    DeleteObject(pen);
                }
            } else {
                DrawTextW(hdc, L"Archivo > Abrir BMP...", -1, &rc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            if (!g_state.hasImage) return 0;
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);

            if (g_state.canvasScale <= 0.0) return 0;
            int px = static_cast<int>((mx - g_state.canvasOffsetX) / g_state.canvasScale);
            int py = static_cast<int>((my - g_state.canvasOffsetY) / g_state.canvasScale);

            if (px >= 0 && px < g_state.img.width() && py >= 0 && py < g_state.img.height()) {
                g_state.selX = px;
                g_state.selY = py;
                g_state.hasSelection = true;
                UpdateSelectedLabel();
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
//  Rueda de color (HSV): un circulo donde el angulo = tono (hue) y la
//  distancia al centro = saturacion; el brillo (value) lo controla el
//  slider vertical aparte.
// ============================================================================

static const int kWheelSize = 180;

static void BuildWheelBitmap(HWND hwnd) {
    HDC hdcScreen = GetDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, kWheelSize, kWheelSize);
    HGDIOBJ old = SelectObject(hdcMem, hBmp);

    double radius = kWheelSize / 2.0;
    double cx = radius, cy = radius;

    // Fondo (fuera del circulo) con el color de la ventana.
    RECT full{0, 0, kWheelSize, kWheelSize};
    FillRect(hdcMem, &full, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));

    for (int y = 0; y < kWheelSize; ++y) {
        for (int x = 0; x < kWheelSize; ++x) {
            double dx = x - cx;
            double dy = y - cy;
            double r = std::sqrt(dx * dx + dy * dy);
            if (r <= radius) {
                double hue = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;
                if (hue < 0) hue += 360.0;
                double sat = (std::min)(1.0, r / radius);
                Pixel p = HsvToRgb(hue, sat, 1.0);
                SetPixel(hdcMem, x, y, RGB(p.r, p.g, p.b));
            }
        }
    }

    SelectObject(hdcMem, old);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcScreen);

    if (g_state.hWheelBitmap) DeleteObject(g_state.hWheelBitmap);
    g_state.hWheelBitmap = hBmp;
}

static void UpdateColorFromWheelAndValue() {
    g_state.currentColor = HsvToRgb(g_state.wheelHue, g_state.wheelSat, g_state.wheelValue);
    UpdateRgbEditsFromCurrentColor();
}

static LRESULT CALLBACK WheelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            BuildWheelBitmap(hwnd);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HDC hdcMem = CreateCompatibleDC(hdc);
            HGDIOBJ old = SelectObject(hdcMem, g_state.hWheelBitmap);
            BitBlt(hdc, 0, 0, kWheelSize, kWheelSize, hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcMem, old);
            DeleteDC(hdcMem);

            if (g_state.hasWheelMarker) {
                HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
                HGDIOBJ oldPen = SelectObject(hdc, pen);
                HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Ellipse(hdc, g_state.wheelMarkerX - 5, g_state.wheelMarkerY - 5,
                        g_state.wheelMarkerX + 5, g_state.wheelMarkerY + 5);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(pen);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            double radius = kWheelSize / 2.0;
            double dx = mx - radius;
            double dy = my - radius;
            double r = std::sqrt(dx * dx + dy * dy);
            if (r > radius) {
                // Clic fuera del circulo: se ajusta a la orilla (misma direccion).
                double scale = radius / (r > 0.0001 ? r : 1.0);
                dx *= scale;
                dy *= scale;
                r = radius;
            }
            double hue = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;
            if (hue < 0) hue += 360.0;
            double sat = (radius > 0) ? (r / radius) : 0.0;

            g_state.wheelHue = hue;
            g_state.wheelSat = sat;
            g_state.hasWheelMarker = true;
            g_state.wheelMarkerX = static_cast<int>(radius + dx);
            g_state.wheelMarkerY = static_cast<int>(radius + dy);

            UpdateColorFromWheelAndValue();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
//  Muestra de color (swatch): un rectangulo solido con el color actual.
// ============================================================================

static LRESULT CALLBACK SwatchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH brush = CreateSolidBrush(
            RGB(g_state.currentColor.r, g_state.currentColor.g, g_state.currentColor.b));
        FillRect(hdc, &rc, brush);
        DeleteObject(brush);
        FrameRect(hdc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
//  Operaciones de archivo (abrir / guardar / guardar como)
// ============================================================================

static void DoOpenFile(HWND hwnd) {
    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Archivos BMP (*.bmp)\0*.bmp\0Todos los archivos\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"bmp";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return;  // el usuario cancelo

    // Convertir la ruta ancha (wide) a std::string (UTF-8/ANSI) para la
    // biblioteca BmpImage, que trabaja con std::string / ifstream.
    int len = WideCharToMultiByte(CP_ACP, 0, fileBuf, -1, nullptr, 0, nullptr, nullptr);
    std::string pathA(len, '\0');
    WideCharToMultiByte(CP_ACP, 0, fileBuf, -1, pathA.data(), len, nullptr, nullptr);
    if (!pathA.empty() && pathA.back() == '\0') pathA.pop_back();

    try {
        BmpImage loaded = BmpImage::load(pathA);
        g_state.img = loaded;
        g_state.original = loaded;
        g_state.hasImage = true;
        g_state.hasSelection = false;
        g_state.unsavedChanges = false;
        g_state.currentPath = fileBuf;
        RefreshAll();
    } catch (const BmpError& e) {
        std::string msg = e.what();
        MessageBoxA(hwnd, msg.c_str(), "Error al abrir el archivo", MB_OK | MB_ICONERROR);
    }
}

static bool SaveTo(const std::wstring& widePath, HWND hwnd) {
    int len = WideCharToMultiByte(CP_ACP, 0, widePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string pathA(len, '\0');
    WideCharToMultiByte(CP_ACP, 0, widePath.c_str(), -1, pathA.data(), len, nullptr, nullptr);
    if (!pathA.empty() && pathA.back() == '\0') pathA.pop_back();

    try {
        g_state.img.save(pathA);
        g_state.currentPath = widePath;
        g_state.unsavedChanges = false;
        RefreshAll();
        return true;
    } catch (const BmpError& e) {
        std::string msg = e.what();
        MessageBoxA(hwnd, msg.c_str(), "Error al guardar", MB_OK | MB_ICONERROR);
        return false;
    }
}

static void DoSave(HWND hwnd) {
    if (!g_state.hasImage) return;
    if (g_state.currentPath.empty()) {
        // Sin ruta previa: se comporta como "Guardar como".
        wchar_t fileBuf[MAX_PATH] = L"";
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"Archivos BMP (*.bmp)\0*.bmp\0";
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"bmp";
        ofn.Flags = OFN_OVERWRITEPROMPT;
        if (!GetSaveFileNameW(&ofn)) return;
        SaveTo(fileBuf, hwnd);
    } else {
        SaveTo(g_state.currentPath, hwnd);
    }
}

static void DoSaveAs(HWND hwnd) {
    if (!g_state.hasImage) return;
    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Archivos BMP (*.bmp)\0*.bmp\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"bmp";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;
    SaveTo(fileBuf, hwnd);
}

// ============================================================================
//  Ventana principal
// ============================================================================

static void CreateAllControls(HWND hwnd, HINSTANCE hInst) {
    // --- Menu ---
    HMENU hMenuBar = CreateMenu();
    HMENU hFileMenu = CreatePopupMenu();
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_OPEN, L"Abrir BMP...");
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_SAVE, L"Guardar");
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_SAVEAS, L"Guardar como...");
    AppendMenuW(hFileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_EXIT, L"Salir");
    AppendMenuW(hMenuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hFileMenu), L"Archivo");
    SetMenu(hwnd, hMenuBar);

    // --- Lienzo (imagen) ---
    g_state.hCanvas = CreateWindowW(L"BmpCanvasClass", nullptr,
                                     WS_CHILD | WS_VISIBLE | WS_BORDER,
                                     10, 10, 640, 640, hwnd,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CANVAS)),
                                     hInst, nullptr);

    // --- Etiqueta de pixel seleccionado ---
    g_state.hLabelSelected = MakeStatic(hwnd, hInst, L"Pixel seleccionado: (ninguno)",
                                         660, 10, 420, 20);

    // --- Rueda de color + slider de brillo (value) ---
    MakeStatic(hwnd, hInst, L"Rueda de color (haz clic para elegir tono/saturacion):",
               660, 40, 420, 18);
    g_state.hWheel = CreateWindowW(L"ColorWheelClass", nullptr, WS_CHILD | WS_VISIBLE,
                                    660, 60, kWheelSize, kWheelSize, hwnd,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_WHEEL)),
                                    hInst, nullptr);

    MakeStatic(hwnd, hInst, L"Brillo", 855, 60, 60, 18);
    g_state.hValueSlider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                            WS_CHILD | WS_VISIBLE | TBS_VERT | TBS_AUTOTICKS,
                                            855, 80, 45, kWheelSize, hwnd,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VALUE_SLIDER)),
                                            hInst, nullptr);
    SendMessageW(g_state.hValueSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 255));
    SendMessageW(g_state.hValueSlider, TBM_SETPOS, TRUE, 255);

    // --- Muestra de color + cuadros RGB manuales ---
    g_state.hSwatch = CreateWindowW(L"SwatchClass", nullptr, WS_CHILD | WS_VISIBLE,
                                     660, 250, 60, 40, hwnd,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SWATCH)),
                                     hInst, nullptr);

    MakeStatic(hwnd, hInst, L"R", 735, 252, 15, 18);
    g_state.hEditR = MakeEdit(hwnd, hInst, L"255", 752, 250, 40, 20, IDC_EDIT_R);
    MakeStatic(hwnd, hInst, L"G", 798, 252, 15, 18);
    g_state.hEditG = MakeEdit(hwnd, hInst, L"0", 815, 250, 40, 20, IDC_EDIT_G);
    MakeStatic(hwnd, hInst, L"B", 861, 252, 15, 18);
    g_state.hEditB = MakeEdit(hwnd, hInst, L"0", 878, 250, 40, 20, IDC_EDIT_B);
    MakeButton(hwnd, hInst, L"Fijar RGB", 925, 250, 90, 22, IDC_BTN_SET_RGB);

    MakeButton(hwnd, hInst, L"Aplicar color al pixel seleccionado",
               660, 280, 300, 26, IDC_BTN_APPLY_PIXEL);

    // --- Operaciones simples (un clic) ---
    int y = 320;
    MakeStatic(hwnd, hInst, L"--- Operaciones ---", 660, y, 300, 18); y += 22;
    MakeButton(hwnd, hInst, L"Escala de grises", 660, y, 140, 24, IDC_BTN_GRAYSCALE);
    MakeButton(hwnd, hInst, L"Invertir colores", 810, y, 140, 24, IDC_BTN_INVERT);
    y += 30;
    MakeButton(hwnd, hInst, L"Espejo horizontal", 660, y, 140, 24, IDC_BTN_FLIPH);
    MakeButton(hwnd, hInst, L"Espejo vertical", 810, y, 140, 24, IDC_BTN_FLIPV);
    y += 30;
    MakeButton(hwnd, hInst, L"Rotar 90 grados", 660, y, 140, 24, IDC_BTN_ROTATE90);
    MakeButton(hwnd, hInst, L"Rotar 180 grados", 810, y, 140, 24, IDC_BTN_ROTATE180);
    y += 36;

    // --- Brillo / contraste ---
    MakeStatic(hwnd, hInst, L"Brillo (delta -255..255):", 660, y, 170, 18);
    g_state.hEditBrightness = MakeEdit(hwnd, hInst, L"30", 830, y - 2, 50, 20, IDC_EDIT_BRIGHTNESS);
    MakeButton(hwnd, hInst, L"Aplicar", 890, y - 3, 60, 22, IDC_BTN_BRIGHTNESS);
    y += 26;
    MakeStatic(hwnd, hInst, L"Contraste (factor, ej. 1.5):", 660, y, 170, 18);
    g_state.hEditContrast = MakeEdit(hwnd, hInst, L"1.5", 830, y - 2, 50, 20, IDC_EDIT_CONTRAST);
    MakeButton(hwnd, hInst, L"Aplicar", 890, y - 3, 60, 22, IDC_BTN_CONTRAST);
    y += 34;

    // --- Recortar ---
    MakeStatic(hwnd, hInst, L"Recortar  x,y,w,h:", 660, y, 120, 18);
    g_state.hEditCropX = MakeEdit(hwnd, hInst, L"0", 780, y - 2, 40, 20, IDC_EDIT_CROP_X);
    g_state.hEditCropY = MakeEdit(hwnd, hInst, L"0", 824, y - 2, 40, 20, IDC_EDIT_CROP_Y);
    g_state.hEditCropW = MakeEdit(hwnd, hInst, L"50", 868, y - 2, 40, 20, IDC_EDIT_CROP_W);
    g_state.hEditCropH = MakeEdit(hwnd, hInst, L"50", 912, y - 2, 40, 20, IDC_EDIT_CROP_H);
    MakeButton(hwnd, hInst, L"Recortar", 956, y - 3, 70, 22, IDC_BTN_CROP);
    y += 30;

    // --- Redimensionar ---
    MakeStatic(hwnd, hInst, L"Redimensionar  w,h:", 660, y, 120, 18);
    g_state.hEditResizeW = MakeEdit(hwnd, hInst, L"100", 800, y - 2, 50, 20, IDC_EDIT_RESIZE_W);
    g_state.hEditResizeH = MakeEdit(hwnd, hInst, L"100", 856, y - 2, 50, 20, IDC_EDIT_RESIZE_H);
    MakeButton(hwnd, hInst, L"Aplicar", 912, y - 3, 60, 22, IDC_BTN_RESIZE);
    y += 34;

    // --- Rectangulo ---
    MakeStatic(hwnd, hInst, L"Rectangulo  x,y,w,h:", 660, y, 120, 18);
    g_state.hEditRectX = MakeEdit(hwnd, hInst, L"10", 790, y - 2, 35, 20, IDC_EDIT_RECT_X);
    g_state.hEditRectY = MakeEdit(hwnd, hInst, L"10", 828, y - 2, 35, 20, IDC_EDIT_RECT_Y);
    g_state.hEditRectW = MakeEdit(hwnd, hInst, L"40", 866, y - 2, 35, 20, IDC_EDIT_RECT_W);
    g_state.hEditRectH = MakeEdit(hwnd, hInst, L"40", 904, y - 2, 35, 20, IDC_EDIT_RECT_H);
    y += 24;
    g_state.hCheckFill = MakeCheckbox(hwnd, hInst, L"Relleno", 660, y, 90, 20, IDC_CHECK_FILL);
    MakeButton(hwnd, hInst, L"Dibujar (usa el color de la rueda)", 760, y - 2, 240, 24, IDC_BTN_RECT);
    y += 34;

    // --- Reemplazar color ---
    MakeStatic(hwnd, hInst,
               L"Reemplazar color: origen = pixel seleccionado, destino = color de la rueda.",
               660, y, 420, 18);
    y += 20;
    MakeStatic(hwnd, hInst, L"Tolerancia:", 660, y, 70, 18);
    g_state.hEditTolerance = MakeEdit(hwnd, hInst, L"0", 735, y - 2, 50, 20, IDC_EDIT_TOLERANCE);
    MakeButton(hwnd, hInst, L"Reemplazar color", 800, y - 3, 150, 22, IDC_BTN_REPLACE);
    y += 32;

    MakeButton(hwnd, hInst, L"Deshacer todos los cambios", 660, y, 200, 26, IDC_BTN_UNDO);
    y += 34;

    // --- Barra de estado ---
    g_state.hLabelStatus = MakeStatic(hwnd, hInst, L"Ningun archivo abierto", 660, y, 420, 20);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_state.hMainWnd = hwnd;
            HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(hwnd, GWLP_HINSTANCE));
            CreateAllControls(hwnd, hInst);
            return 0;
        }

        case WM_HSCROLL: {
            if (reinterpret_cast<HWND>(lParam) == g_state.hValueSlider) {
                int pos = static_cast<int>(SendMessageW(g_state.hValueSlider, TBM_GETPOS, 0, 0));
                g_state.wheelValue = pos / 255.0;
                UpdateColorFromWheelAndValue();
            }
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int notify = HIWORD(wParam);

            if (notify != BN_CLICKED && id != ID_FILE_OPEN && id != ID_FILE_SAVE &&
                id != ID_FILE_SAVEAS && id != ID_FILE_EXIT) {
                break;
            }

            switch (id) {
                case ID_FILE_OPEN: DoOpenFile(hwnd); break;
                case ID_FILE_SAVE: DoSave(hwnd); break;
                case ID_FILE_SAVEAS: DoSaveAs(hwnd); break;
                case ID_FILE_EXIT: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;

                case IDC_BTN_SET_RGB: {
                    int r = GetEditInt(g_state.hEditR, 0);
                    int g = GetEditInt(g_state.hEditG, 0);
                    int b = GetEditInt(g_state.hEditB, 0);
                    auto clampByte = [](int v) { return static_cast<uint8_t>((std::max)(0, (std::min)(255, v))); };
                    g_state.currentColor = Pixel{clampByte(r), clampByte(g), clampByte(b)};
                    InvalidateRect(g_state.hSwatch, nullptr, TRUE);
                    break;
                }

                case IDC_BTN_APPLY_PIXEL: {
                    if (g_state.hasImage && g_state.hasSelection) {
                        g_state.img.set(g_state.selX, g_state.selY, g_state.currentColor);
                        g_state.unsavedChanges = true;
                        RefreshAll();
                    }
                    break;
                }

                case IDC_BTN_GRAYSCALE:
                    if (g_state.hasImage) { g_state.img.to_grayscale(); g_state.unsavedChanges = true; RefreshAll(); }
                    break;
                case IDC_BTN_INVERT:
                    if (g_state.hasImage) { g_state.img.invert(); g_state.unsavedChanges = true; RefreshAll(); }
                    break;
                case IDC_BTN_FLIPH:
                    if (g_state.hasImage) { g_state.img.flip_horizontal(); g_state.unsavedChanges = true; RefreshAll(); }
                    break;
                case IDC_BTN_FLIPV:
                    if (g_state.hasImage) { g_state.img.flip_vertical(); g_state.unsavedChanges = true; RefreshAll(); }
                    break;
                case IDC_BTN_ROTATE90:
                    if (g_state.hasImage) {
                        g_state.img.rotate90_cw();
                        g_state.hasSelection = false;
                        g_state.unsavedChanges = true;
                        RefreshAll();
                    }
                    break;
                case IDC_BTN_ROTATE180:
                    if (g_state.hasImage) { g_state.img.rotate180(); g_state.unsavedChanges = true; RefreshAll(); }
                    break;

                case IDC_BTN_BRIGHTNESS:
                    if (g_state.hasImage) {
                        g_state.img.adjust_brightness(GetEditInt(g_state.hEditBrightness, 0));
                        g_state.unsavedChanges = true;
                        RefreshAll();
                    }
                    break;

                case IDC_BTN_CONTRAST:
                    if (g_state.hasImage) {
                        g_state.img.adjust_contrast(GetEditDouble(g_state.hEditContrast, 1.0));
                        g_state.unsavedChanges = true;
                        RefreshAll();
                    }
                    break;

                case IDC_BTN_CROP:
                    if (g_state.hasImage) {
                        try {
                            g_state.img.crop(GetEditInt(g_state.hEditCropX, 0),
                                              GetEditInt(g_state.hEditCropY, 0),
                                              GetEditInt(g_state.hEditCropW, 1),
                                              GetEditInt(g_state.hEditCropH, 1));
                            g_state.hasSelection = false;
                            g_state.unsavedChanges = true;
                            RefreshAll();
                        } catch (const BmpError& e) {
                            MessageBoxA(hwnd, e.what(), "Error al recortar", MB_OK | MB_ICONERROR);
                        }
                    }
                    break;

                case IDC_BTN_RESIZE:
                    if (g_state.hasImage) {
                        try {
                            g_state.img.resize_nearest(GetEditInt(g_state.hEditResizeW, 1),
                                                        GetEditInt(g_state.hEditResizeH, 1));
                            g_state.hasSelection = false;
                            g_state.unsavedChanges = true;
                            RefreshAll();
                        } catch (const BmpError& e) {
                            MessageBoxA(hwnd, e.what(), "Error al redimensionar", MB_OK | MB_ICONERROR);
                        }
                    }
                    break;

                case IDC_BTN_RECT:
                    if (g_state.hasImage) {
                        bool filled = SendMessageW(g_state.hCheckFill, BM_GETCHECK, 0, 0) == BST_CHECKED;
                        g_state.img.draw_rectangle(GetEditInt(g_state.hEditRectX, 0),
                                                    GetEditInt(g_state.hEditRectY, 0),
                                                    GetEditInt(g_state.hEditRectW, 1),
                                                    GetEditInt(g_state.hEditRectH, 1),
                                                    g_state.currentColor, filled);
                        g_state.unsavedChanges = true;
                        RefreshAll();
                    }
                    break;

                case IDC_BTN_REPLACE:
                    if (g_state.hasImage) {
                        if (!g_state.hasSelection) {
                            MessageBoxW(hwnd,
                                        L"Primero haz clic en un pixel del color que quieres reemplazar.",
                                        L"BmpEditor", MB_OK | MB_ICONINFORMATION);
                        } else {
                            Pixel from = g_state.img.get(g_state.selX, g_state.selY);
                            int tol = GetEditInt(g_state.hEditTolerance, 0);
                            int changed = g_state.img.replace_color(from, g_state.currentColor, tol);
                            g_state.unsavedChanges = g_state.unsavedChanges || (changed > 0);
                            RefreshAll();
                            wchar_t buf[128];
                            swprintf(buf, 128, L"%d pixel(es) reemplazados.", changed);
                            MessageBoxW(hwnd, buf, L"Reemplazar color", MB_OK | MB_ICONINFORMATION);
                        }
                    }
                    break;

                case IDC_BTN_UNDO:
                    if (g_state.hasImage) {
                        g_state.img = g_state.original;
                        g_state.hasSelection = false;
                        g_state.unsavedChanges = false;
                        RefreshAll();
                    }
                    break;
            }
            return 0;
        }

        case WM_CLOSE: {
            if (g_state.unsavedChanges) {
                int r = MessageBoxW(hwnd,
                                     L"Tienes cambios sin guardar. ¿Salir de todas formas?",
                                     L"BmpEditor", MB_YESNO | MB_ICONWARNING);
                if (r != IDYES) return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY:
            if (g_state.hWheelBitmap) DeleteObject(g_state.hWheelBitmap);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
//  Punto de entrada
// ============================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"BmpEditorMainClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    WNDCLASSW wcCanvas = {};
    wcCanvas.lpfnWndProc = CanvasProc;
    wcCanvas.hInstance = hInstance;
    wcCanvas.lpszClassName = L"BmpCanvasClass";
    wcCanvas.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wcCanvas.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wcCanvas);

    WNDCLASSW wcWheel = {};
    wcWheel.lpfnWndProc = WheelProc;
    wcWheel.hInstance = hInstance;
    wcWheel.lpszClassName = L"ColorWheelClass";
    wcWheel.hCursor = LoadCursor(nullptr, IDC_HAND);
    RegisterClassW(&wcWheel);

    WNDCLASSW wcSwatch = {};
    wcSwatch.lpfnWndProc = SwatchProc;
    wcSwatch.hInstance = hInstance;
    wcSwatch.lpszClassName = L"SwatchClass";
    RegisterClassW(&wcSwatch);

    HWND hwnd = CreateWindowW(L"BmpEditorMainClass", L"BmpEditor",
                               WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
                               CW_USEDEFAULT, CW_USEDEFAULT, 1120, 780,
                               nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
