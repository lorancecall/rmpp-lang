#!/bin/sh
# Download the Korean font from the official Noto CJK repository (SIL Open Font License 1.1).
# The device already ships "Noto Sans KR" (variable); this adds "Noto Sans CJK KR" Regular and Bold.
set -e
out=${1:-build/fonts}
mkdir -p "$out"
base=https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/Korean
for f in NotoSansCJKkr-Regular.otf NotoSansCJKkr-Bold.otf; do
  [ -s "$out/$f" ] || curl -sfL -o "$out/$f" "$base/$f"
done
[ -s "$out/LICENSE" ] || curl -sfL -o "$out/LICENSE" https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/LICENSE
