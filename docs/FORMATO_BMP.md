# Notas técnicas del formato BMP (Windows Bitmap)

Referencia rápida del subconjunto del formato BMP soportado por BmpEditor:
BMP sin comprimir, 24 bits por píxel (`BI_RGB`).

## Estructura general de un archivo .bmp

```
Offset   Tamaño    Bloque                  Descripción
------   -------   ---------------------   -------------------------------
0        14 bytes  BITMAPFILEHEADER        Firma, tamaño total, offset de datos
14       40 bytes  BITMAPINFOHEADER        Dimensiones, bpp, compresión
54       variable  Datos de píxeles        Filas de píxeles con padding
```

## BITMAPFILEHEADER (14 bytes)

| Offset | Tamaño | Campo | Descripción |
|---|---|---|---|
| 0 | 2 | `bfType` | Firma ASCII, debe ser `"BM"` |
| 2 | 4 | `bfSize` | Tamaño total del archivo en bytes |
| 6 | 2 | `bfReserved1` | Reservado (0) |
| 8 | 2 | `bfReserved2` | Reservado (0) |
| 10 | 4 | `bfOffBits` | Offset donde comienzan los datos de píxeles (normalmente 54) |

## BITMAPINFOHEADER (40 bytes)

| Offset | Tamaño | Campo | Descripción |
|---|---|---|---|
| 14 | 4 | `biSize` | Tamaño de este header (40) |
| 18 | 4 | `biWidth` | Ancho en píxeles (con signo) |
| 22 | 4 | `biHeight` | Alto en píxeles; **positivo = bottom-up, negativo = top-down** |
| 26 | 2 | `biPlanes` | Siempre 1 |
| 28 | 2 | `biBitCount` | Bits por píxel (BmpEditor solo soporta 24) |
| 30 | 4 | `biCompression` | 0 = `BI_RGB` (sin comprimir); BmpEditor solo soporta este valor |
| 34 | 4 | `biSizeImage` | Tamaño de los datos de píxel en bytes |
| 38 | 4 | `biXPelsPerMeter` | Resolución horizontal |
| 42 | 4 | `biYPelsPerMeter` | Resolución vertical |
| 46 | 4 | `biClrUsed` | Colores en la paleta (0 si no aplica) |
| 50 | 4 | `biClrImportant` | Colores "importantes" (0 = todos) |

## Datos de píxeles

- Cada píxel ocupa 3 bytes, en orden **B, G, R** (no R, G, B).
- Cada fila se rellena (*padding*) con bytes 0x00 hasta ser múltiplo de 4
  bytes: `stride = ((width * 3 + 3) / 4) * 4`.
- Las filas se almacenan de abajo hacia arriba (la primera fila del
  archivo es la parte inferior de la imagen) salvo que `biHeight` sea
  negativo, en cuyo caso se almacenan de arriba hacia abajo.

## Ejemplo numérico

Para una imagen de 37 píxeles de ancho:

```
37 * 3 = 111 bytes de datos de color por fila
stride = ((111 + 3) / 4) * 4 = 112 bytes
padding = 112 - 111 = 1 byte de relleno por fila
```

BmpEditor calcula este valor con la función `row_stride()` y lo usa tanto
al leer (para saltar el padding) como al escribir (para generarlo).
