# BmpEditor

**BmpEditor** es una aplicación nativa de línea de comandos, escrita en
C++17, que implementa un **editor de imágenes BMP (Windows Bitmap)**
manipulando el formato a **bajo nivel**: parseo manual de los encabezados
binarios (`BITMAPFILEHEADER`, `BITMAPINFOHEADER`), cálculo del *padding* de
cada fila y acceso directo a la matriz de píxeles cruda.

## Características

- **Lectura/escritura binaria del formato BMP** implementada desde cero,
  byte a byte (sin librerías de imágenes de terceros como `stb_image` o
  `libpng`), soportando BMP de 24 bits por píxel sin compresión (`BI_RGB`),
  el caso más común.
- Manejo correcto de detalles de bajo nivel del formato:
  - orden de canales **B, G, R** (no R, G, B) tal como exige la
    especificación,
  - filas almacenadas **de abajo hacia arriba** (bottom-up) salvo que la
    altura sea negativa (top-down),
  - **padding de cada fila a múltiplos de 4 bytes**.
- Operaciones de edición implementadas manipulando directamente los
  píxeles:
  - `grayscale` — escala de grises (luminancia perceptual ITU-R BT.601)
  - `invert` — inversión de colores
  - `flip-h` / `flip-v` — espejo horizontal / vertical
  - `rotate90` / `rotate180` — rotación
  - `brightness <delta>` — ajuste de brillo
  - `contrast <factor>` — ajuste de contraste
  - `crop <x> <y> <w> <h>` — recorte
  - `resize <w> <h>` — redimensionado (vecino más cercano)
  - `rect <x> <y> <w> <h> <r> <g> <b> <fill|outline>` — dibujar rectángulo
  - `setpixel <x> <y> <r> <g> <b>` — cambiar un solo píxel (equivalente a `set` en modo interactivo)
  - `replace <r1> <g1> <b1> <r2> <g2> <b2> [tolerancia]` — reemplazar un color por otro en toda la imagen
  - `info` — inspeccionar dimensiones y metadatos del archivo

## Estructura del proyecto

```
BmpEditor/
├── CMakeLists.txt              # Sistema de build (CMake)
├── README.md
├── include/
│   └── bmp_image.hpp           # Interfaz pública: clase BmpImage
├── src/
│   ├── bmp_image.cpp           # Parseo binario BMP + operaciones de edicion
│   └── main.cpp                # CLI
├── tests/
│   └── test_bmp_image.cpp      # 11 pruebas unitarias
└── docs/
    ├── DESARROLLO.md           # Documentacion del proceso de desarrollo
    └── FORMATO_BMP.md          # Notas tecnicas del formato BMP
```

## Compilación (CMake)

Requisitos: CMake >= 3.15, compilador C++17 (GCC, Clang o MSVC).

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
ctest --output-on-failure   # ejecuta las 11 pruebas unitarias
```

Genera dos binarios en `build/`: `bmpedit` (la aplicación) y
`bmpeditor_tests` (pruebas).

## Interfaz gráfica para Windows (GUI nativa, Win32 + GDI)

Además del CLI, el proyecto incluye una **aplicación de escritorio para
Windows** (`bmpedit_gui`) con interfaz gráfica completa, construida en Win32
puro (sin frameworks externos) reutilizando directamente la misma biblioteca
`bmpeditor_lib` (la clase `BmpImage`) que usa el CLI — es decir, toda la
lógica de bajo nivel del formato BMP y las operaciones de edición son
exactamente el mismo código ya probado con las 12 pruebas unitarias.

**Solo se compila en Windows** (usa `<windows.h>`, cuadros de diálogo
nativos, etc.); en Linux/macOS ese target se omite automáticamente gracias
a un bloque `if(WIN32)` en `CMakeLists.txt`, así que no afecta la
compilación del CLI en otros sistemas.

### Qué incluye

- **Archivo > Abrir BMP...** — selector de archivos nativo de Windows.
- **Lienzo interactivo**: la imagen se dibuja escalada para verse completa;
  al hacer **clic sobre cualquier pixel** queda seleccionado (recuadro
  amarillo) y se puede leer o cambiar su color.
- **Rueda de color HSV**: un círculo donde el ángulo elige el tono (hue) y
  la distancia al centro la saturación; un control deslizante vertical
  aparte controla el brillo (value). También hay 3 cuadros de texto R/G/B
  para escribir un color exacto a mano.
- **"Aplicar color al pixel seleccionado"**: pinta el pixel que clicaste con
  el color elegido en la rueda.
- **Botones para todas las operaciones ya existentes en el CLI**: escala de
  grises, invertir, espejo horizontal/vertical, rotar 90°/180°, brillo,
  contraste, recortar, redimensionar, dibujar rectángulo (relleno o solo
  borde) y **reemplazar color** (usa el pixel seleccionado como color de
  origen y el color de la rueda como destino, con tolerancia opcional).
- **Deshacer**: revierte todos los cambios al estado en que se abrió el
  archivo.
- **Guardar / Guardar como**, con aviso si intentas cerrar la ventana con
  cambios sin guardar.

### Cómo compilarla y abrirla en Visual Studio

La forma más simple es reutilizar el mismo proyecto CMake que ya tienes:

1. Abre Visual Studio → **Archivo > Abrir > Carpeta...** y selecciona la
   carpeta raíz de `BmpEditor` (donde está `CMakeLists.txt`). Visual Studio
   detecta automáticamente el proyecto CMake.
2. En la barra de herramientas, en el desplegable de "elemento de inicio",
   elige **`bmpedit_gui.exe`**.
3. Compila con **Ctrl+Shift+B** y ejecuta con **F5** (o Ctrl+F5 sin
   depurador).

También puedes compilarla desde PowerShell igual que el CLI:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\bmpedit_gui.exe
```

> **Nota:** esta parte del código no se pudo compilar ni probar en el
> entorno donde se generó este proyecto (es Linux, y el código usa la API
> nativa de Windows). Se escribió con cuidado siguiendo los patrones
> estándar de Win32, pero si al compilarla en tu Visual Studio aparece
> algún error, compártemelo con el mensaje exacto y lo corregimos.

## Modo interactivo (editar en consola, pixel a pixel)

Además de aplicar una sola operación por comando, BmpEditor tiene un
**modo interactivo** pensado exactamente para esto: ir cambiando colores
uno por uno (o en bloque, con reemplazo de color) desde la consola, viendo
el resultado, y decidiendo cuándo guardar:

```bash
./bmpedit edit foto.bmp
```

Dentro de la sesión:

```
bmpedit> get 10 20
Pixel (10,20) = R:220 G:60 B:60

bmpedit> set 10 20 0 255 0
Pixel (10,20) actualizado a R:0 G:255 B:0

bmpedit> replace 220 60 60 0 255 0
99 pixel(es) cambiados de RGB(220,60,60) a RGB(0,255,0)

bmpedit> replace 220 60 60 0 255 0 15
# el ultimo numero es una tolerancia: reemplaza tambien colores "parecidos"

bmpedit> save
Guardado en: foto.bmp
# sin argumento, "save" sobrescribe directamente el archivo que abriste

bmpedit> save copia.bmp
Guardado en: copia.bmp
# con argumento, guarda una copia aparte sin tocar el original

bmpedit> undo
Todos los cambios fueron descartados (imagen restaurada al original).

bmpedit> exit
```

Si intentas salir (`exit`/`quit`) con cambios sin guardar, el programa te
pregunta `¿Salir sin guardar? (s/n)` antes de cerrar, para no perder trabajo
por accidente.

**Importante:** `save` sin argumento **modifica el archivo original**
(lo sobrescribe con los cambios hechos en la sesión). Si prefieres conservar
el original intacto, usa `save <nombre_nuevo.bmp>` para guardar una copia
aparte.

Comandos disponibles en el modo interactivo: `get`, `set`, `replace`,
`info`, `save`, `undo`, `help`, `exit`/`quit`.

## Uso (modo de un solo comando / scripting)

```bash
# Ver metadatos de un BMP
./bmpedit info foto.bmp

# Escala de grises
./bmpedit foto.bmp foto_gris.bmp grayscale

# Rotar 90 grados
./bmpedit foto.bmp foto_rot.bmp rotate90

# Recortar una region de 100x80 desde (40,20)
./bmpedit foto.bmp foto_recorte.bmp crop 40 20 100 80

# Dibujar un rectangulo amarillo sin relleno
./bmpedit foto.bmp foto_rect.bmp rect 10 10 60 40 255 255 0 outline

# Ajustar brillo (+30) y contraste (x1.4)
./bmpedit foto.bmp foto_brillo.bmp brightness 30
./bmpedit foto.bmp foto_contraste.bmp contrast 1.4
```

### Resultado de ejemplo

Imagen original de prueba (tres franjas de color) y el resultado de aplicar
`grayscale`, `rotate90`, `crop` y `rect`:

| Original | Grayscale | Rotate90 | Crop | Rect |
|---|---|---|---|---|
| franjas R/G/B | tonos de gris | franjas horizontales | region recortada | borde amarillo |

(ver `docs/DESARROLLO.md` para las imágenes y el detalle de cada prueba)

## Diseño técnico (resumen)

La clase `BmpImage` separa completamente el **formato de archivo** (bytes en
disco) del **modelo en memoria** (una matriz simple de píxeles RGB
top-down, sin padding), de forma que todas las operaciones de edición
trabajan sobre una representación sencilla, y solo `load()` / `save()`
conocen los detalles binarios del formato BMP (orden de canales, bottom-up,
padding). Esto separa claramente "bajo nivel de E/S" de "lógica de edición".

Más detalle del proceso de desarrollo (decisiones, un bug real detectado y
corregido durante las pruebas, y las imágenes de validación) en
[`docs/DESARROLLO.md`](docs/DESARROLLO.md). Notas técnicas del formato BMP
en [`docs/FORMATO_BMP.md`](docs/FORMATO_BMP.md).

## Licencia

Proyecto académico de uso educativo.
