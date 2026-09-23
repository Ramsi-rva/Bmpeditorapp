// bmp_image.cpp
// Implementacion de bajo nivel del formato BMP: lectura/escritura de los
// encabezados binarios byte a byte (sin depender de structs empaquetados
// dependientes de compilador), calculo manual del padding de fila, y
// operaciones de edicion de pixeles.

#include "bmp_image.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace bmpeditor {

namespace {

// --- Utilidades de lectura/escritura binaria little-endian (formato BMP) ---

uint16_t read_u16le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t read_u32le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24));
}
int32_t read_i32le(const uint8_t* p) {
    return static_cast<int32_t>(read_u32le(p));
}

void write_u16le(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
void write_u32le(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}
void write_i32le(std::vector<uint8_t>& buf, int32_t v) {
    write_u32le(buf, static_cast<uint32_t>(v));
}

// Tamano de fila en el archivo BMP, incluyendo el padding a multiplo de 4.
size_t row_stride(int width) {
    return (static_cast<size_t>(width) * 3 + 3) & ~static_cast<size_t>(3);
}

}  // namespace

uint8_t BmpImage::clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<uint8_t>(v);
}

BmpImage BmpImage::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw BmpError("No se pudo abrir el archivo: " + path);

    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());

    if (raw.size() < 54) {
        throw BmpError("Archivo demasiado pequeno para ser un BMP valido.");
    }

    // --- BITMAPFILEHEADER (14 bytes) ---
    if (raw[0] != 'B' || raw[1] != 'M') {
        throw BmpError("Firma invalida: no es un archivo BMP (se esperaba 'BM').");
    }
    uint32_t data_offset = read_u32le(&raw[10]);

    // --- BITMAPINFOHEADER (al menos 40 bytes) ---
    uint32_t dib_header_size = read_u32le(&raw[14]);
    if (dib_header_size < 40) {
        throw BmpError("Encabezado DIB no soportado (se requiere BITMAPINFOHEADER >= 40 bytes).");
    }
    int32_t width_raw = read_i32le(&raw[18]);
    int32_t height_raw = read_i32le(&raw[22]);
    uint16_t planes = read_u16le(&raw[26]);
    uint16_t bpp = read_u16le(&raw[28]);
    uint32_t compression = read_u32le(&raw[30]);

    if (planes != 1) {
        throw BmpError("Numero de planos no soportado (se esperaba 1).");
    }
    if (bpp != 24) {
        throw BmpError("Solo se soportan BMP de 24 bits por pixel (bpp=" +
                        std::to_string(bpp) + " no soportado).");
    }
    if (compression != 0) {
        throw BmpError("Solo se soporta BMP sin compresion (BI_RGB).");
    }

    bool top_down = height_raw < 0;
    int width = width_raw;
    int height = top_down ? -height_raw : height_raw;

    if (width <= 0 || height <= 0) {
        throw BmpError("Dimensiones invalidas en el encabezado BMP.");
    }

    size_t stride = row_stride(width);
    size_t needed = static_cast<size_t>(data_offset) + stride * static_cast<size_t>(height);
    if (raw.size() < needed) {
        throw BmpError("El archivo esta truncado: faltan datos de pixeles.");
    }

    BmpImage img;
    img.width_ = width;
    img.height_ = height;
    img.pixels_.resize(static_cast<size_t>(width) * height * 3);

    // BMP almacena filas de abajo hacia arriba salvo que height sea negativo.
    for (int row = 0; row < height; ++row) {
        int src_row = top_down ? row : (height - 1 - row);
        const uint8_t* row_ptr = &raw[data_offset + static_cast<size_t>(src_row) * stride];
        for (int x = 0; x < width; ++x) {
            // BMP almacena los canales en orden B, G, R.
            uint8_t b = row_ptr[x * 3 + 0];
            uint8_t g = row_ptr[x * 3 + 1];
            uint8_t r = row_ptr[x * 3 + 2];
            size_t idx = img.index(x, row);
            img.pixels_[idx + 0] = r;
            img.pixels_[idx + 1] = g;
            img.pixels_[idx + 2] = b;
        }
    }

    return img;
}

BmpImage BmpImage::create(int width, int height, Pixel fill) {
    if (width <= 0 || height <= 0) {
        throw BmpError("Dimensiones invalidas al crear una imagen nueva.");
    }
    BmpImage img;
    img.width_ = width;
    img.height_ = height;
    img.pixels_.resize(static_cast<size_t>(width) * height * 3);
    for (size_t i = 0; i < img.pixels_.size(); i += 3) {
        img.pixels_[i] = fill.r;
        img.pixels_[i + 1] = fill.g;
        img.pixels_[i + 2] = fill.b;
    }
    return img;
}

void BmpImage::save(const std::string& path) const {
    if (width_ <= 0 || height_ <= 0) {
        throw BmpError("No se puede guardar una imagen con dimensiones invalidas.");
    }

    size_t stride = row_stride(width_);
    size_t pixel_data_size = stride * static_cast<size_t>(height_);
    uint32_t data_offset = 54;  // 14 (file header) + 40 (info header)
    uint32_t file_size = static_cast<uint32_t>(data_offset + pixel_data_size);

    std::vector<uint8_t> out;
    out.reserve(file_size);

    // --- BITMAPFILEHEADER ---
    out.push_back('B');
    out.push_back('M');
    write_u32le(out, file_size);
    write_u32le(out, 0);              // reservado
    write_u32le(out, data_offset);

    // --- BITMAPINFOHEADER ---
    write_u32le(out, 40);             // tamano del header
    write_i32le(out, width_);
    write_i32le(out, height_);        // positivo -> bottom-up
    write_u16le(out, 1);              // planos
    write_u16le(out, 24);             // bpp
    write_u32le(out, 0);              // sin compresion
    write_u32le(out, static_cast<uint32_t>(pixel_data_size));
    write_i32le(out, 2835);           // ~72 DPI horizontal
    write_i32le(out, 2835);           // ~72 DPI vertical
    write_u32le(out, 0);              // colores en paleta
    write_u32le(out, 0);              // colores importantes

    // --- Datos de pixeles, bottom-up, con padding por fila ---
    std::vector<uint8_t> row_buf(stride, 0);
    for (int row = height_ - 1; row >= 0; --row) {
        std::fill(row_buf.begin(), row_buf.end(), 0);
        for (int x = 0; x < width_; ++x) {
            size_t idx = index(x, row);
            row_buf[x * 3 + 0] = pixels_[idx + 2];  // B
            row_buf[x * 3 + 1] = pixels_[idx + 1];  // G
            row_buf[x * 3 + 2] = pixels_[idx + 0];  // R
        }
        out.insert(out.end(), row_buf.begin(), row_buf.end());
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) throw BmpError("No se pudo crear el archivo de salida: " + path);
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
}

Pixel BmpImage::get(int x, int y) const {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
        throw BmpError("Coordenada fuera de rango en get().");
    }
    size_t idx = index(x, y);
    return Pixel{pixels_[idx], pixels_[idx + 1], pixels_[idx + 2]};
}

void BmpImage::set(int x, int y, const Pixel& p) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;  // fuera de rango: no-op
    size_t idx = index(x, y);
    pixels_[idx] = p.r;
    pixels_[idx + 1] = p.g;
    pixels_[idx + 2] = p.b;
}

void BmpImage::to_grayscale() {
    for (size_t i = 0; i < pixels_.size(); i += 3) {
        // Luminancia perceptual (ITU-R BT.601).
        double lum = 0.299 * pixels_[i] + 0.587 * pixels_[i + 1] + 0.114 * pixels_[i + 2];
        uint8_t g = clamp_u8(static_cast<int>(std::lround(lum)));
        pixels_[i] = pixels_[i + 1] = pixels_[i + 2] = g;
    }
}

void BmpImage::invert() {
    for (auto& channel : pixels_) {
        channel = static_cast<uint8_t>(255 - channel);
    }
}

void BmpImage::flip_horizontal() {
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_ / 2; ++x) {
            size_t a = index(x, y);
            size_t b = index(width_ - 1 - x, y);
            for (int c = 0; c < 3; ++c) std::swap(pixels_[a + c], pixels_[b + c]);
        }
    }
}

void BmpImage::flip_vertical() {
    for (int y = 0; y < height_ / 2; ++y) {
        for (int x = 0; x < width_; ++x) {
            size_t a = index(x, y);
            size_t b = index(x, height_ - 1 - y);
            for (int c = 0; c < 3; ++c) std::swap(pixels_[a + c], pixels_[b + c]);
        }
    }
}

void BmpImage::rotate90_cw() {
    std::vector<uint8_t> rotated(pixels_.size());
    int new_w = height_;
    int new_h = width_;
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            // (x, y) en la original -> (new_w - 1 - y, x) en la rotada
            size_t src = index(x, y);
            int nx = new_w - 1 - y;
            int ny = x;
            size_t dst = (static_cast<size_t>(ny) * new_w + nx) * 3;
            rotated[dst] = pixels_[src];
            rotated[dst + 1] = pixels_[src + 1];
            rotated[dst + 2] = pixels_[src + 2];
        }
    }
    pixels_ = std::move(rotated);
    width_ = new_w;
    height_ = new_h;
}

void BmpImage::rotate180() {
    flip_horizontal();
    flip_vertical();
}

void BmpImage::adjust_brightness(int delta) {
    for (auto& channel : pixels_) {
        channel = clamp_u8(static_cast<int>(channel) + delta);
    }
}

void BmpImage::adjust_contrast(double factor) {
    // Formula estandar: nuevo = (viejo - 128) * factor + 128
    for (auto& channel : pixels_) {
        double v = (static_cast<double>(channel) - 128.0) * factor + 128.0;
        channel = clamp_u8(static_cast<int>(std::lround(v)));
    }
}

void BmpImage::crop(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > width_ || y + h > height_) {
        throw BmpError("Region de recorte fuera de los limites de la imagen.");
    }
    std::vector<uint8_t> cropped(static_cast<size_t>(w) * h * 3);
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            size_t src = index(x + col, y + row);
            size_t dst = (static_cast<size_t>(row) * w + col) * 3;
            cropped[dst] = pixels_[src];
            cropped[dst + 1] = pixels_[src + 1];
            cropped[dst + 2] = pixels_[src + 2];
        }
    }
    pixels_ = std::move(cropped);
    width_ = w;
    height_ = h;
}

void BmpImage::resize_nearest(int new_w, int new_h) {
    if (new_w <= 0 || new_h <= 0) throw BmpError("Dimensiones de redimensionado invalidas.");
    std::vector<uint8_t> resized(static_cast<size_t>(new_w) * new_h * 3);
    for (int y = 0; y < new_h; ++y) {
        int src_y = static_cast<int>((static_cast<double>(y) * height_) / new_h);
        src_y = std::min(src_y, height_ - 1);
        for (int x = 0; x < new_w; ++x) {
            int src_x = static_cast<int>((static_cast<double>(x) * width_) / new_w);
            src_x = std::min(src_x, width_ - 1);
            size_t src = index(src_x, src_y);
            size_t dst = (static_cast<size_t>(y) * new_w + x) * 3;
            resized[dst] = pixels_[src];
            resized[dst + 1] = pixels_[src + 1];
            resized[dst + 2] = pixels_[src + 2];
        }
    }
    pixels_ = std::move(resized);
    width_ = new_w;
    height_ = new_h;
}

void BmpImage::draw_rectangle(int x, int y, int w, int h, Pixel color, bool filled) {
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(width_ - 1, x + w - 1);
    int y1 = std::min(height_ - 1, y + h - 1);

    for (int row = y0; row <= y1; ++row) {
        for (int col = x0; col <= x1; ++col) {
            bool border = (row == y0 || row == y1 || col == x0 || col == x1);
            if (filled || border) {
                set(col, row, color);
            }
        }
    }
}

int BmpImage::replace_color(Pixel from, Pixel to, int tolerance) {
    int changed = 0;
    for (size_t i = 0; i < pixels_.size(); i += 3) {
        int dr = std::abs(static_cast<int>(pixels_[i]) - from.r);
        int dg = std::abs(static_cast<int>(pixels_[i + 1]) - from.g);
        int db = std::abs(static_cast<int>(pixels_[i + 2]) - from.b);
        if (dr + dg + db <= tolerance) {
            pixels_[i] = to.r;
            pixels_[i + 1] = to.g;
            pixels_[i + 2] = to.b;
            ++changed;
        }
    }
    return changed;
}

std::string BmpImage::describe() const {
    std::ostringstream oss;
    oss << "Dimensiones: " << width_ << " x " << height_ << " pixeles\n"
        << "Formato: BMP 24 bpp, sin compresion (BI_RGB)\n"
        << "Tamano de fila con padding: " << row_stride(width_) << " bytes\n"
        << "Tamano de datos de pixel: " << (row_stride(width_) * static_cast<size_t>(height_))
        << " bytes";
    return oss.str();
}

}  // namespace bmpeditor
