#!/bin/sh
# Build a language add-on the way the package build does (docs/DESIGN.md, "Language add-ons"):
#   build/languages/<code>/      -> /home/root/.local/share/rmpp-lang/languages/<code>/ on the tablet
#   build/languages/<code>.rcc   -> languages/<code>.rcc next to rmpp-lang.rcc (only when keyboards/ exists)
# Usage: sh tools/build-addon.sh CODE...      (fonts.conf downloads are checked against their SHA-256)
set -e
cd "$(dirname "$0")/.."
RCC=${RCC:-uvx --from pyside6-essentials==6.10.1 pyside6-rcc}
for code in "$@"; do
  src=languages/$code
  [ -f "$src/language.conf" ] || { echo "no $src/language.conf" >&2; exit 1; }
  grep -q "^code=$code\$" "$src/language.conf" || { echo "$src/language.conf: code is not $code" >&2; exit 1; }
  # Summaries become the package description: apk's pkgdesc must stay under 128 bytes.
  grep '^summary_' "$src/language.conf" | while IFS= read -r line; do
    n=$(printf '%s' "${line#*=}" | wc -c)
    [ "$n" -lt 128 ] || { echo "$src/language.conf: ${line%%=*} is $n bytes (must be < 128)" >&2; exit 1; }
  done
  # Shared code set (rmpp-hwr-db scripts/languages.py): ISO 639-1, or a listed variant such as zh-hant.
  case "$code" in en|latin) echo "$code is reserved for the core" >&2; exit 1 ;; esac
  echo "$code" | grep -Eq '^[a-z]{2}(-[a-z]{2,8})?$' || { echo "$code: not an ISO 639-1 code (or variant)" >&2; exit 1; }
  out=build/languages/$code
  rm -rf "$out" "build/languages/$code.rcc"
  mkdir -p "$out"
  cp "$src/language.conf" "$out/"
  for f in "$src"/keymap*.conf; do [ -f "$f" ] && cp "$f" "$out/"; done   # keymap@<method>= files too
  [ -d "$src/dict" ] && mkdir -p "$out/dict" && cp "$src"/dict/* "$out/dict/"
  [ -d "$src/fonts" ] && mkdir -p "$out/fonts" && cp "$src"/fonts/* "$out/fonts/"
  [ -f "$src/LICENSE-fonts" ] && cp "$src/LICENSE-fonts" "$out/"
  [ -f "$src/glyphs.rmgl" ] && cp "$src/glyphs.rmgl" "$out/"   # stroke glyphs (tools/glyphs/glyphs.py)
  if [ -f "$src/fonts.conf" ]; then
    mkdir -p "$out/fonts"
    grep -v '^\s*#' "$src/fonts.conf" | while read -r url sum name; do
      [ -n "$url" ] || continue
      curl -sfL -o "$out/fonts/$name" "$url"
      echo "$sum  $out/fonts/$name" | sha256sum -c - >/dev/null || { echo "checksum mismatch: $name" >&2; exit 1; }
    done
  fi
  if [ -d "$src/keyboards" ]; then
    qrc=build/languages/$code.qrc
    { echo '<RCC><qresource prefix="/">'
      for f in "$src"/keyboards/*/keyboard_layout.json; do
        l=$(basename "$(dirname "$f")")
        echo "<file alias=\"misc/keyboards/$l/keyboard_layout.json\">../../$f</file>"
      done
      echo '</qresource></RCC>'; } > "$qrc"
    (cd build/languages && $RCC --binary --no-compress "$code.qrc" -o "$code.rcc")
    rm -f "$qrc"
  fi
  # Layouts of one input method (keyboards@<method>/<layout>/): <code>/keyboards@<method>.rcc in the /home part,
  # registered by the core over <code>.rcc while that method is chosen.
  for kd in "$src"/keyboards@*/; do
    [ -d "$kd" ] || continue
    m=$(basename "$kd")
    qrc=build/languages/$code/$m.qrc
    { echo '<RCC><qresource prefix="/">'
      for f in "$kd"*/keyboard_layout.json; do
        l=$(basename "$(dirname "$f")")
        echo "<file alias=\"misc/keyboards/$l/keyboard_layout.json\">../../../$src/$m/$l/keyboard_layout.json</file>"
      done
      echo '</qresource></RCC>'; } > "$qrc"
    (cd "build/languages/$code" && $RCC --binary --no-compress "$m.qrc" -o "$m.rcc")
    rm -f "$qrc"
  done
  echo "built $out$( [ -f build/languages/$code.rcc ] && echo " + build/languages/$code.rcc")"
done
