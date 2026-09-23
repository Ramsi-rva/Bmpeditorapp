// test_bmp_image.cpp
// Pruebas unitarias minimas (sin frameworks externos) para BmpEditor.
// Se generan imagenes BMP sinteticas en memoria/disco y se validan las
// distintas operaciones de edicion, incluyendo round-trip de carga/guardado.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#include "bmp_image.hpp"

using namespace bmpeditor;

namespace {

int failures = 0;

void check(bool condition, const std::string& name) {
    std::cout << "[" << (condition ? "OK  " : "FAIL") << "] " << name << "\n";
    if (!condition) ++failures;
}

// Crea una imagen de prueba con un patron reconocible:
// mitad izquierda roja, mitad derecha azul.
BmpImage make_test_image(int w, int h) {
    BmpImage img = BmpImage::create(w, h, Pixel{0, 0, 0});
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (x < w / 2) {
                img.set(x, y, Pixel{255, 0, 0});
            } else {
                img.set(x, y, Pixel{0, 0, 255});
            }
        }
    }
    return img;
}

}  // namespace

int main() {
    // --- 1. Round-trip: guardar y volver a cargar debe preservar los pixeles ---
    {
        auto img = make_test_image(40, 20);
        img.save("/tmp/bmp_test_roundtrip.bmp");
        auto loaded = BmpImage::load("/tmp/bmp_test_roundtrip.bmp");

        bool ok = (loaded.width() == 40 && loaded.height() == 20);
        for (int y = 0; ok && y < 20; ++y) {
            for (int x = 0; ok && x < 40; ++x) {
                Pixel a = img.get(x, y);
                Pixel b = loaded.get(x, y);
                if (a.r != b.r || a.g != b.g || a.b != b.b) ok = false;
            }
        }
        check(ok, "round-trip guardar/cargar preserva los pixeles exactamente");
    }

    // --- 2. Round-trip con ancho NO multiplo de 4 (fuerza padding real) ---
    {
        auto img = make_test_image(37, 13);  // 37*3=111 bytes -> necesita padding
        img.save("/tmp/bmp_test_padding.bmp");
        auto loaded = BmpImage::load("/tmp/bmp_test_padding.bmp");
        bool ok = (loaded.width() == 37 && loaded.height() == 13);
        for (int y = 0; ok && y < 13; ++y) {
            for (int x = 0; ok && x < 37; ++x) {
                Pixel a = img.get(x, y), b = loaded.get(x, y);
                if (a.r != b.r || a.g != b.g || a.b != b.b) ok = false;
            }
        }
        check(ok, "round-trip con ancho no multiplo de 4 (padding de fila) funciona");
    }

    // --- 3. Grayscale: cada canal debe quedar igual dentro de cada pixel ---
    {
        auto img = make_test_image(10, 10);
        img.to_grayscale();
        bool ok = true;
        for (int y = 0; y < 10 && ok; ++y)
            for (int x = 0; x < 10 && ok; ++x) {
                Pixel p = img.get(x, y);
                if (!(p.r == p.g && p.g == p.b)) ok = false;
            }
        check(ok, "grayscale iguala los 3 canales en cada pixel");
    }

    // --- 4. Invert es su propia inversa ---
    {
        auto img = make_test_image(10, 10);
        auto original = img;
        img.invert();
        img.invert();
        bool ok = true;
        for (int y = 0; y < 10 && ok; ++y)
            for (int x = 0; x < 10 && ok; ++x) {
                Pixel a = img.get(x, y), b = original.get(x, y);
                if (a.r != b.r || a.g != b.g || a.b != b.b) ok = false;
            }
        check(ok, "invertir dos veces reproduce la imagen original");
    }

    // --- 5. Flip horizontal invierte columnas ---
    {
        auto img = make_test_image(10, 4);  // mitad izq roja, mitad der azul
        img.flip_horizontal();
        Pixel left = img.get(0, 0);
        Pixel right = img.get(9, 0);
        check(left.b == 255 && right.r == 255, "flip-h intercambia izquierda/derecha");
    }

    // --- 6. Rotar 90 grados intercambia ancho y alto ---
    {
        auto img = make_test_image(30, 10);
        img.rotate90_cw();
        check(img.width() == 10 && img.height() == 30, "rotate90 intercambia ancho/alto");
    }

    // --- 7. Rotar 180 dos veces = identidad ---
    {
        auto img = make_test_image(12, 8);
        auto original = img;
        img.rotate180();
        img.rotate180();
        bool ok = true;
        for (int y = 0; y < 8 && ok; ++y)
            for (int x = 0; x < 12 && ok; ++x) {
                Pixel a = img.get(x, y), b = original.get(x, y);
                if (a.r != b.r || a.g != b.g || a.b != b.b) ok = false;
            }
        check(ok, "rotar 180 dos veces reproduce la imagen original");
    }

    // --- 8. Crop produce las dimensiones y contenido esperados ---
    {
        auto img = make_test_image(20, 20);
        img.crop(5, 5, 8, 6);
        check(img.width() == 8 && img.height() == 6, "crop produce las dimensiones esperadas");
    }

    // --- 9. Brightness satura correctamente en 0 y 255 ---
    {
        auto img = BmpImage::create(4, 4, Pixel{200, 10, 250});
        img.adjust_brightness(100);
        Pixel p = img.get(0, 0);
        check(p.r == 255 && p.g == 110 && p.b == 255, "brightness satura en 255 sin desbordar");
    }

    // --- 10. Resize cambia dimensiones correctamente ---
    {
        auto img = make_test_image(40, 20);
        img.resize_nearest(20, 10);
        check(img.width() == 20 && img.height() == 10, "resize_nearest produce las dimensiones pedidas");
    }

    // --- 11. Formato invalido debe lanzar BmpError ---
    {
        std::ofstream bad("/tmp/bmp_test_invalid.bin", std::ios::binary);
        bad << "no es un bmp";
        bad.close();
        bool threw = false;
        try {
            BmpImage::load("/tmp/bmp_test_invalid.bin");
        } catch (const BmpError&) {
            threw = true;
        }
        check(threw, "cargar un archivo no-BMP lanza BmpError");
    }

    // --- 12. replace_color reemplaza exactamente los pixeles esperados ---
    {
        auto img = make_test_image(10, 4);  // mitad izq roja, mitad der azul
        int changed = img.replace_color(Pixel{255, 0, 0}, Pixel{0, 255, 0}, 0);
        Pixel left = img.get(0, 0);
        Pixel right = img.get(9, 0);
        check(changed == 20 && left.g == 255 && right.b == 255,
              "replace_color cambia solo los pixeles del color buscado (20 de 40)");
    }

    if (failures == 0) {
        std::cout << "\nTodas las pruebas pasaron correctamente.\n";
        return 0;
    }
    std::cout << "\n" << failures << " prueba(s) fallaron.\n";
    return 1;
}
