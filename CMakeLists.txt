cmake_minimum_required(VERSION 3.15)
project(BmpEditor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# --- Biblioteca principal (funciones de bajo nivel sobre el formato BMP) ---
add_library(bmpeditor_lib
    src/bmp_image.cpp
)
target_include_directories(bmpeditor_lib PUBLIC include)

# --- Ejecutable CLI ---
add_executable(bmpedit
    src/main.cpp
)
target_link_libraries(bmpedit PRIVATE bmpeditor_lib)

# --- Pruebas ---
enable_testing()
add_executable(bmpeditor_tests
    tests/test_bmp_image.cpp
)
target_link_libraries(bmpeditor_tests PRIVATE bmpeditor_lib)
add_test(NAME BmpImageTests COMMAND bmpeditor_tests)

# --- Interfaz grafica (GUI) para Windows ---
# Usa la API nativa de Windows (Win32 + GDI), por lo que solo se compila
# cuando el destino de build es Windows (p. ej. desde Visual Studio).
# En Linux/macOS este bloque simplemente se ignora.
if(WIN32)
    add_executable(bmpedit_gui WIN32
        src/gui_main.cpp
    )
    target_link_libraries(bmpedit_gui PRIVATE bmpeditor_lib
        comctl32
        comdlg32
        gdi32
        user32
    )
endif()

install(TARGETS bmpedit DESTINATION bin)
