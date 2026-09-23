# CMake generated Testfile for 
# Source directory: C:/Documents/Programacion del Sistema/BmpEditor
# Build directory: C:/Documents/Programacion del Sistema/BmpEditor/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test(BmpImageTests "C:/Documents/Programacion del Sistema/BmpEditor/build/Debug/bmpeditor_tests.exe")
  set_tests_properties(BmpImageTests PROPERTIES  _BACKTRACE_TRIPLES "C:/Documents/Programacion del Sistema/BmpEditor/CMakeLists.txt;35;add_test;C:/Documents/Programacion del Sistema/BmpEditor/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test(BmpImageTests "C:/Documents/Programacion del Sistema/BmpEditor/build/Release/bmpeditor_tests.exe")
  set_tests_properties(BmpImageTests PROPERTIES  _BACKTRACE_TRIPLES "C:/Documents/Programacion del Sistema/BmpEditor/CMakeLists.txt;35;add_test;C:/Documents/Programacion del Sistema/BmpEditor/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test(BmpImageTests "C:/Documents/Programacion del Sistema/BmpEditor/build/MinSizeRel/bmpeditor_tests.exe")
  set_tests_properties(BmpImageTests PROPERTIES  _BACKTRACE_TRIPLES "C:/Documents/Programacion del Sistema/BmpEditor/CMakeLists.txt;35;add_test;C:/Documents/Programacion del Sistema/BmpEditor/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test(BmpImageTests "C:/Documents/Programacion del Sistema/BmpEditor/build/RelWithDebInfo/bmpeditor_tests.exe")
  set_tests_properties(BmpImageTests PROPERTIES  _BACKTRACE_TRIPLES "C:/Documents/Programacion del Sistema/BmpEditor/CMakeLists.txt;35;add_test;C:/Documents/Programacion del Sistema/BmpEditor/CMakeLists.txt;0;")
else()
  add_test(BmpImageTests NOT_AVAILABLE)
endif()
