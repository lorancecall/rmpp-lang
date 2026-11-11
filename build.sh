#!/bin/sh
# Cross-build librmpp-lang.so and rmpp-lang.rcc (the core). Language add-ons: tools/build-addon.sh CODE.
# SDK defaults to ../sdk (Arm GNU aarch64-none-linux-gnu + Qt 6.10 headers + device Qt libs in sdk/devlibs).
set -e
cd "$(dirname "$0")"
SDK=${SDK:-../sdk}
CXX=${CXX:-$SDK/bin/aarch64-none-linux-gnu-g++}
Q=$SDK/qt/usr/include/qt6
mkdir -p build
"$CXX" -std=c++17 -O2 -fPIC -shared -DQT_NO_VERSION_TAGGING -DQT_NO_DEBUG \
  -I"$Q" -I"$Q/QtCore" -I"$Q/QtGui" -I"$Q/QtQml" -I"$Q/QtQuick" src/lang.cpp src/hangul.cpp src/hanja.cpp -o build/librmpp-lang.so \
  -L"$SDK/devlibs" -l:libQt6Core.so.6 -l:libQt6Gui.so.6 -l:libQt6Qml.so.6 -l:libQt6Quick.so.6 -ldl \
  -Wl,--allow-shlib-undefined -Wl,-soname,librmpp-lang.so
${RCC:-uvx --from pyside6-essentials==6.10.1 pyside6-rcc} --binary --no-compress rmpp-lang.qrc -o build/rmpp-lang.rcc
cp tools/rmpp-lang build/rmpp-lang
ls -la build
