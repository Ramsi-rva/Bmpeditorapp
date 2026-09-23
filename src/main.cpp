// main.cpp
// BmpEditor CLI
//
// Modos de uso:
//
//   1) Un solo comando (por lotes / scripting):
//      bmpedit info <entrada.bmp>
//      bmpedit <entrada.bmp> <salida.bmp> <operacion> [args...]
//
//   2) Modo INTERACTIVO en consola (edicion pixel a pixel / reemplazo de color):
//      bmpedit edit <entrada.bmp>
//      Dentro de la sesion, escribes comandos como:
//        get 10 20
//        set 10 20 255 0 0
//        replace 255 0 0  0 0 255
//        replace 255 0 0  0 0 255 20      (con tolerancia)
//        save                              (sobrescribe el archivo original)
//        save otro_nombre.bmp              (guarda una copia con otro nombre)
//        exit
//
// Operaciones disponibles en modo de un solo comando:
//   grayscale | invert | flip-h | flip-v | rotate90 | rotate180
//   brightness <delta> | contrast <factor>
//   crop <x> <y> <w> <h> | resize <w> <h>
//   rect <x> <y> <w> <h> <r> <g> <b> <fill|outline>
//   setpixel <x> <y> <r> <g> <b>
//   replace <r1> <g1> <b1> <r2> <g2> <b2> [tolerancia]

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "bmp_image.hpp"

namespace {

using namespace bmpeditor;

void print_usage(const char* prog) {
    std::cout <<
        "BmpEditor - editor nativo de imagenes BMP (manipulacion a bajo nivel)\n\n"
        "Uso:\n"
        "  " << prog << " info <entrada.bmp>\n"
        "  " << prog << " edit <entrada.bmp>                     (modo interactivo)\n"
        "  " << prog << " <entrada.bmp> <salida.bmp> <operacion> [args...]\n\n"
        "Operaciones (modo de un solo comando):\n"
        "  grayscale\n"
        "  invert\n"
        "  flip-h\n"
        "  flip-v\n"
        "  rotate90\n"
        "  rotate180\n"
        "  brightness <delta>            (-255..255)\n"
        "  contrast <factor>             (ej. 1.5)\n"
        "  crop <x> <y> <w> <h>\n"
        "  resize <w> <h>\n"
        "  rect <x> <y> <w> <h> <r> <g> <b> <fill|outline>\n"
        "  setpixel <x> <y> <r> <g> <b>\n"
        "  replace <r1> <g1> <b1> <r2> <g2> <b2> [tolerancia]\n\n"
        "Modo interactivo: " << prog << " edit <entrada.bmp>\n"
        "  Permite editar la imagen bit a bit / pixel a pixel desde la consola\n"
        "  antes de guardarla, con los comandos: get, set, replace, info, save,\n"
        "  undo, help, exit.\n";
}

void print_interactive_help() {
    std::cout <<
        "\nComandos disponibles:\n"
        "  get <x> <y>                                  Muestra el color actual del pixel\n"
        "  set <x> <y> <r> <g> <b>                       Cambia el color de un pixel\n"
        "  replace <r1> <g1> <b1> <r2> <g2> <b2> [tol]   Reemplaza un color por otro\n"
        "                                                 en toda la imagen (tol = tolerancia,\n"
        "                                                 0 = coincidencia exacta)\n"
        "  info                                          Muestra dimensiones y formato\n"
        "  save [archivo.bmp]                            Guarda los cambios; sin argumento\n"
        "                                                 sobrescribe el archivo original cargado\n"
        "  undo                                          Descarta todos los cambios sin guardar\n"
        "  help                                          Muestra esta ayuda\n"
        "  exit | quit                                   Sale del modo interactivo\n\n";
}

// Modo interactivo: permite al usuario ir cambiando pixeles/colores desde la
// consola, viendo el resultado antes de decidir guardarlo a disco.
int run_interactive_mode(const std::string& input_path) {
    BmpImage img = BmpImage::load(input_path);
    const BmpImage original = img;  // copia para poder deshacer todo (undo)

    bool unsaved_changes = false;

    std::cout << "=== BmpEditor - modo interactivo ===\n";
    std::cout << "Archivo cargado: " << input_path << "\n";
    std::cout << img.describe() << "\n";
    print_interactive_help();

    std::string line;
    while (true) {
        std::cout << "bmpedit> ";
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;  // EOF (p. ej. Ctrl+Z / Ctrl+D)
        }

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd.empty()) continue;

        try {
            if (cmd == "exit" || cmd == "quit") {
                if (unsaved_changes) {
                    std::cout << "Tienes cambios sin guardar. ¿Salir sin guardar? (s/n): ";
                    std::string confirm;
                    if (std::getline(std::cin, confirm) && !confirm.empty() &&
                        (confirm[0] == 's' || confirm[0] == 'S')) {
                        break;
                    }
                    std::cout << "Cancelado. Sigues en modo edicion.\n";
                    continue;
                }
                break;
            }

            if (cmd == "help") {
                print_interactive_help();

            } else if (cmd == "info") {
                std::cout << img.describe() << "\n";

            } else if (cmd == "get") {
                int x, y;
                if (!(iss >> x >> y)) {
                    std::cout << "Uso: get <x> <y>\n";
                } else {
                    Pixel p = img.get(x, y);
                    std::cout << "Pixel (" << x << "," << y << ") = R:" << static_cast<int>(p.r)
                              << " G:" << static_cast<int>(p.g) << " B:" << static_cast<int>(p.b) << "\n";
                }

            } else if (cmd == "set") {
                int x, y, r, g, b;
                if (!(iss >> x >> y >> r >> g >> b)) {
                    std::cout << "Uso: set <x> <y> <r> <g> <b>\n";
                } else {
                    img.set(x, y, Pixel{static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                         static_cast<uint8_t>(b)});
                    unsaved_changes = true;
                    std::cout << "Pixel (" << x << "," << y << ") actualizado a R:" << r
                              << " G:" << g << " B:" << b << "\n";
                }

            } else if (cmd == "replace") {
                int r1, g1, b1, r2, g2, b2;
                int tol = 0;
                if (!(iss >> r1 >> g1 >> b1 >> r2 >> g2 >> b2)) {
                    std::cout << "Uso: replace <r1> <g1> <b1> <r2> <g2> <b2> [tolerancia]\n";
                } else {
                    iss >> tol;  // opcional; si no viene, queda en 0
                    int changed = img.replace_color(
                        Pixel{static_cast<uint8_t>(r1), static_cast<uint8_t>(g1), static_cast<uint8_t>(b1)},
                        Pixel{static_cast<uint8_t>(r2), static_cast<uint8_t>(g2), static_cast<uint8_t>(b2)},
                        tol);
                    unsaved_changes = unsaved_changes || (changed > 0);
                    std::cout << changed << " pixel(es) cambiados de RGB(" << r1 << "," << g1 << "," << b1
                              << ") a RGB(" << r2 << "," << g2 << "," << b2 << ")"
                              << (tol > 0 ? " [tolerancia " + std::to_string(tol) + "]" : "") << "\n";
                }

            } else if (cmd == "save") {
                std::string out_path;
                if (!(iss >> out_path)) {
                    out_path = input_path;  // sin argumento: sobrescribe el original
                }
                img.save(out_path);
                unsaved_changes = false;
                std::cout << "Guardado en: " << out_path << "\n";

            } else if (cmd == "undo") {
                img = original;
                unsaved_changes = false;
                std::cout << "Todos los cambios fueron descartados (imagen restaurada al original).\n";

            } else {
                std::cout << "Comando desconocido: '" << cmd << "'. Escribe 'help' para ver los comandos.\n";
            }

        } catch (const BmpError& e) {
            std::cout << "Error: " << e.what() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Error: entrada invalida (" << e.what() << ")\n";
        }
    }

    std::cout << "Sesion de edicion finalizada.\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string first = argv[1];

    try {
        if (first == "-h" || first == "--help") {
            print_usage(argv[0]);
            return 0;
        }

        if (first == "info") {
            if (argc != 3) { print_usage(argv[0]); return 1; }
            BmpImage img = BmpImage::load(argv[2]);
            std::cout << img.describe() << "\n";
            return 0;
        }

        if (first == "edit") {
            if (argc != 3) {
                std::cerr << "Uso: " << argv[0] << " edit <entrada.bmp>\n";
                return 1;
            }
            return run_interactive_mode(argv[2]);
        }

        if (argc < 4) {
            print_usage(argv[0]);
            return 1;
        }

        std::string input = argv[1];
        std::string output = argv[2];
        std::string op = argv[3];

        BmpImage img = BmpImage::load(input);

        if (op == "grayscale") {
            img.to_grayscale();
        } else if (op == "invert") {
            img.invert();
        } else if (op == "flip-h") {
            img.flip_horizontal();
        } else if (op == "flip-v") {
            img.flip_vertical();
        } else if (op == "rotate90") {
            img.rotate90_cw();
        } else if (op == "rotate180") {
            img.rotate180();
        } else if (op == "brightness") {
            if (argc != 5) throw BmpError("brightness requiere <delta>");
            img.adjust_brightness(std::stoi(argv[4]));
        } else if (op == "contrast") {
            if (argc != 5) throw BmpError("contrast requiere <factor>");
            img.adjust_contrast(std::stod(argv[4]));
        } else if (op == "crop") {
            if (argc != 8) throw BmpError("crop requiere <x> <y> <w> <h>");
            img.crop(std::stoi(argv[4]), std::stoi(argv[5]), std::stoi(argv[6]), std::stoi(argv[7]));
        } else if (op == "resize") {
            if (argc != 6) throw BmpError("resize requiere <w> <h>");
            img.resize_nearest(std::stoi(argv[4]), std::stoi(argv[5]));
        } else if (op == "rect") {
            if (argc != 12) throw BmpError("rect requiere <x> <y> <w> <h> <r> <g> <b> <fill|outline>");
            Pixel color{static_cast<uint8_t>(std::stoi(argv[8])),
                        static_cast<uint8_t>(std::stoi(argv[9])),
                        static_cast<uint8_t>(std::stoi(argv[10]))};
            bool filled = std::string(argv[11]) == "fill";
            img.draw_rectangle(std::stoi(argv[4]), std::stoi(argv[5]), std::stoi(argv[6]),
                                std::stoi(argv[7]), color, filled);
        } else if (op == "setpixel") {
            if (argc != 9) throw BmpError("setpixel requiere <x> <y> <r> <g> <b>");
            img.set(std::stoi(argv[4]), std::stoi(argv[5]),
                    Pixel{static_cast<uint8_t>(std::stoi(argv[6])),
                          static_cast<uint8_t>(std::stoi(argv[7])),
                          static_cast<uint8_t>(std::stoi(argv[8]))});
        } else if (op == "replace") {
            if (argc != 10 && argc != 11) {
                throw BmpError("replace requiere <r1> <g1> <b1> <r2> <g2> <b2> [tolerancia]");
            }
            int tol = (argc == 11) ? std::stoi(argv[10]) : 0;
            int changed = img.replace_color(
                Pixel{static_cast<uint8_t>(std::stoi(argv[4])), static_cast<uint8_t>(std::stoi(argv[5])),
                      static_cast<uint8_t>(std::stoi(argv[6]))},
                Pixel{static_cast<uint8_t>(std::stoi(argv[7])), static_cast<uint8_t>(std::stoi(argv[8])),
                      static_cast<uint8_t>(std::stoi(argv[9]))},
                tol);
            std::cout << changed << " pixel(es) reemplazados.\n";
        } else {
            std::cerr << "Operacion desconocida: " << op << "\n\n";
            print_usage(argv[0]);
            return 1;
        }

        img.save(output);
        std::cout << "Operacion '" << op << "' aplicada correctamente.\n";
        std::cout << img.describe() << "\n";
        return 0;

    } catch (const BmpError& e) {
        std::cerr << "Error de BmpEditor: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Error inesperado: " << e.what() << "\n";
        return 3;
    }
}
