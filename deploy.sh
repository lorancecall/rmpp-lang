#!/bin/sh
# Development install over ssh (default host alias "rmpp"), then restart xochitl.
#   sh deploy.sh [HOST]            install build/ the way the package does (rmpp-preload's one-folder layout:
#                                  everything of rmpp-lang in /usr/lib/rmpp/rmpp-lang, add-on data in /home)
#   sh deploy.sh [HOST] --remove   take a hand-made install out again (not for Vellum installs: use vellum)
#   ADDONS="ko ja" sh deploy.sh    language add-ons to install (default: ko; built with tools/build-addon.sh)
# Needs rmpp-preload with the /usr/lib/rmpp layout (its launcher reads /usr/lib/rmpp/preload.d).
# Over a Vellum install this replaces the package's files; `vellum fix` (or the next upgrade) puts them back.
# Never `systemctl stop xochitl`: stopping it powers the tablet off. reset-failed avoids the restart limit (4 / 10 min).
# The root file system is read-only: remount rw, write .new then mv, remount ro after the restart (the running
# xochitl keeps the old library mapped, so ro fails before it).
set -e
cd "$(dirname "$0")"
HOST=${1:-rmpp}
ADDONS=${ADDONS-ko}
D=/usr/lib/rmpp/rmpp-lang
P=/usr/lib/rmpp/preload.d
if [ "$2" = "--remove" ]; then
  ssh "$HOST" "D=$D P=$P"'; set -e
mount -o remount,rw /
rm -f $P/50-rmpp-lang.so
rm -rf $D
sync; systemctl reset-failed xochitl || true; systemctl restart xochitl; sleep 8
mount -o remount,ro / || echo "WARN: root still rw"
systemctl is-active xochitl'
  exit 0
fi
ssh "$HOST" "[ -d $P ]" || { echo "rmpp-preload's $P is missing on $HOST (install rmpp-preload first)" >&2; exit 1; }
scp -q build/librmpp-lang.so build/rmpp-lang.rcc build/rmpp-lang "$HOST":/home/root/
ssh "$HOST" "rm -rf /home/root/rmpp-lang-layouts; mkdir -p /home/root/rmpp-lang-layouts"
for code in $ADDONS; do
  [ -f build/languages/$code/language.conf ] && [ build/languages/$code/language.conf -nt languages/$code/language.conf ] || sh tools/build-addon.sh "$code"
  ssh "$HOST" "rm -rf /home/root/.local/share/rmpp-lang/languages/$code; mkdir -p /home/root/.local/share/rmpp-lang/languages"
  scp -q -r build/languages/$code "$HOST":/home/root/.local/share/rmpp-lang/languages/
  [ -f build/languages/$code.rcc ] && scp -q build/languages/$code.rcc "$HOST":/home/root/rmpp-lang-layouts/
done
ssh "$HOST" "D=$D P=$P ADDONS='$ADDONS'"'; set -e
mount -o remount,rw /
mkdir -p $D/languages
for f in librmpp-lang.so rmpp-lang.rcc rmpp-lang; do cp /home/root/$f $D/$f.new; mv $D/$f.new $D/$f; done
chmod 755 $D/librmpp-lang.so $D/rmpp-lang
for r in /home/root/rmpp-lang-layouts/*.rcc; do
  [ -f "$r" ] || continue
  cp "$r" $D/languages/$(basename "$r").new; mv $D/languages/$(basename "$r").new $D/languages/$(basename "$r")
done
rm -rf /home/root/librmpp-lang.so /home/root/rmpp-lang.rcc /home/root/rmpp-lang /home/root/rmpp-lang-layouts
ln -sfn ../rmpp-lang/librmpp-lang.so $P/50-rmpp-lang.so
for code in $ADDONS; do $D/rmpp-lang lang on $code >/dev/null; done
sync
systemctl reset-failed xochitl || true
systemctl restart xochitl
sleep 8
mount -o remount,ro / || echo "WARN: root still rw"
systemctl is-active xochitl'
