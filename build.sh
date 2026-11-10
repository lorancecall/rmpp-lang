#!/bin/sh
# Cross-build libhangul.so and ko_KR.rcc, and fetch the Korean font.
# SDK defaults to ../sdk (Arm GNU aarch64-none-linux-gnu + Qt 6.10 headers + device Qt libs in sdk/devlibs).
set -e
cd "$(dirname "$0")"
SDK=${SDK:-../sdk}
CXX=${CXX:-$SDK/bin/aarch64-none-linux-gnu-g++}
Q=$SDK/qt/usr/include/qt6
mkdir -p build
"$CXX" -std=c++17 -O2 -fPIC -shared -DQT_NO_VERSION_TAGGING -DQT_NO_DEBUG \
  -I"$Q" -I"$Q/QtCore" -I"$Q/QtGui" src/hangul.cpp -o build/libhangul.so \
  -L"$SDK/devlibs" -l:libQt6Core.so.6 -l:libQt6Gui.so.6 \
  -Wl,--allow-shlib-undefined -Wl,-soname,libhangul.so
(cd layout && ${RCC:-pyside6-rcc} --binary --no-compress ko_KR.qrc -o ../build/ko_KR.rcc)
sh fonts/fetch.sh build/fonts
ls -la build build/fonts
