// bmp_image.hpp
// BmpEditor - Editor nativo de imagenes BMP (Windows Bitmap) a bajo nivel.
//
// Se manipula directamente el encabezado binario del formato BMP
// (BITMAPFILEHEADER + BITMAPINFOHEADER) y la matriz de pixeles cruda,
// incluyendo el padding de cada fila a multiplos de 4 bytes tal como lo
// exige la especificacion oficial del formato.
//
// Soporta BMP sin compresion de 24 bits por pixel (BI_RGB), el caso mas
// comun y suficiente para demostrar el manejo de formatos a bajo nivel.

#ifndef BMPEDITOR_BMP_IMAGE_HPP
#define BMPEDITOR_BMP_IMAGE_HPP

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace bmpeditor {

// Excepcion especifica del dominio.
class BmpError : public std::runtime_error {
public:
    explicit BmpError(const std::string& msg) : std::runtime_error(msg) {}
};

struct Pixel {
    uint8_t r = 0, g = 0, b = 0;
};

// Representa una imagen BMP de 24 bpp ya decodificada en memoria.
// Los pixeles se almacenan en orden top-down (fila 0 = fila superior de la
// imagen), en formato RGB, SIN padding — el padding se recalcula al guardar.
class BmpImage {
public:
    // Carga un archivo .bmp desde disco. Lanza BmpError si el formato no es
    // soportado (solo 24 bpp, sin compresion) o si el archivo esta corrupto.
    static BmpImage load(const std::string& path);

    // Crea una imagen nueva en blanco (util para pruebas y para generar
    // imagenes sinteticas), rellena con el color indicado.
    static BmpImage create(int width, int height, Pixel fill = Pixel{255, 255, 255});

    // Guarda la imagen en disco en formato BMP de 24 bpp, bottom-up,
    // con el padding de fila recalculado segun la especificacion.
    void save(const std::string& path) const;

    int width() const { return width_; }
    int height() const { return height_; }

    Pixel get(int x, int y) const;
    void set(int x, int y, const Pixel& p);

    // Acceso de solo lectura al buffer crudo (RGB, top-down, sin padding).
    // Se usa para renderizado eficiente en la interfaz grafica.
    const std::vector<uint8_t>& raw_rgb() const { return pixels_; }

    // --- Operaciones de edicion (todas modifican la imagen in-place) ---
    void to_grayscale();
    void invert();
    void flip_horizontal();
    void flip_vertical();
    void rotate90_cw();
    void rotate180();
    void adjust_brightness(int delta);       // delta en [-255, 255]
    void adjust_contrast(double factor);     // factor > 0; 1.0 = sin cambio
    void crop(int x, int y, int w, int h);
    void resize_nearest(int new_w, int new_h);
    void draw_rectangle(int x, int y, int w, int h, Pixel color, bool filled);

    // Reemplaza todos los pixeles cuyo color coincide con "from" (dentro de
    // la tolerancia dada) por el color "to". La tolerancia se mide como la
    // suma de diferencias absolutas por canal (0 = coincidencia exacta).
    // Devuelve la cantidad de pixeles modificados.
    int replace_color(Pixel from, Pixel to, int tolerance = 0);

    // Info legible para el comando "info".
    std::string describe() const;

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> pixels_;  // tamano = width_ * height_ * 3, RGB, top-down

    size_t index(int x, int y) const { return (static_cast<size_t>(y) * width_ + x) * 3; }
    static uint8_t clamp_u8(int v);
};

}  // namespace bmpeditor

#endif  // BMPEDITOR_BMP_IMAGE_HPP
