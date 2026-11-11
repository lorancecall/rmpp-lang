#!/bin/sh
# Development install over ssh (default host alias "rmpp"), then restart xochitl.
#   sh deploy.sh [HOST]            install build/ as rmpp-lang; sets aside rmpp-korean's preload link (both would
#                                  compose Hangul twice)
#   sh deploy.sh [HOST] --remove   take rmpp-lang out and put rmpp-korean's link back
#   ADDONS="ja ru" sh deploy.sh    also install these language add-ons (built with tools/build-addon.sh)
# Never `systemctl stop xochitl`: stopping it powers the tablet off. reset-failed avoids the restart limit (4 / 10 min).
set -e
cd "$(dirname "$0")"
HOST=${1:-rmpp}
if [ "$2" = "--remove" ]; then
  ssh "$HOST" 'set -e
mount -o remount,rw /
rm -f /usr/lib/rmpp-preload.d/50-rmpp-lang.so
[ -e /usr/lib/rmpp-lang/50-rmpp-korean.so.off ] && mv /usr/lib/rmpp-lang/50-rmpp-korean.so.off /usr/lib/rmpp-preload.d/50-rmpp-korean.so
rm -rf /usr/lib/rmpp-lang /usr/bin/rmpp-lang
sync; systemctl reset-failed xochitl || true; systemctl restart xochitl; sleep 8
mount -o remount,ro / || echo "WARN: root still rw"
systemctl is-active xochitl'
  exit 0
fi
scp -q build/librmpp-lang.so build/rmpp-lang.rcc build/rmpp-lang "$HOST":/home/root/
ssh "$HOST" 'mkdir -p /home/root/.local/share/fonts/rmpp-lang/ko'
# Korean font: reuse rmpp-korean's copy if present, else upload.
ssh "$HOST" 'ls /home/root/.local/share/fonts/rmpp-lang/ko/*.otf >/dev/null 2>&1 || ls /home/root/.local/share/fonts/rmpp-korean/*.otf >/dev/null 2>&1' \
  || scp -q build/fonts/*.otf build/fonts/LICENSE "$HOST":/home/root/.local/share/fonts/rmpp-lang/ko/
for code in $ADDONS; do
  [ -f build/languages/$code/language.conf ] || sh tools/build-addon.sh "$code"
  ssh "$HOST" "rm -rf /home/root/.local/share/rmpp-lang/languages/$code; mkdir -p /home/root/.local/share/rmpp-lang/languages"
  scp -q -r build/languages/$code "$HOST":/home/root/.local/share/rmpp-lang/languages/
  [ -f build/languages/$code.rcc ] && scp -q build/languages/$code.rcc "$HOST":/home/root/$code.rcc
done
ssh "$HOST" "ADDONS='$ADDONS'"'; set -e
mount -o remount,rw /
mkdir -p /usr/lib/rmpp-lang/languages
for code in $ADDONS; do
  if [ -f /home/root/$code.rcc ]; then cp /home/root/$code.rcc /usr/lib/rmpp-lang/languages/$code.rcc.new; mv /usr/lib/rmpp-lang/languages/$code.rcc.new /usr/lib/rmpp-lang/languages/$code.rcc; rm /home/root/$code.rcc; fi
done
for f in librmpp-lang.so rmpp-lang.rcc; do cp /home/root/$f /usr/lib/rmpp-lang/$f.new; mv /usr/lib/rmpp-lang/$f.new /usr/lib/rmpp-lang/$f; done
cp /home/root/rmpp-lang /usr/bin/rmpp-lang.new; chmod 755 /usr/bin/rmpp-lang.new; mv /usr/bin/rmpp-lang.new /usr/bin/rmpp-lang
rm -f /home/root/librmpp-lang.so /home/root/rmpp-lang.rcc /home/root/rmpp-lang
for code in $ADDONS; do /usr/bin/rmpp-lang lang on $code >/dev/null; done
if [ -L /usr/lib/rmpp-preload.d/50-rmpp-korean.so ]; then mv /usr/lib/rmpp-preload.d/50-rmpp-korean.so /usr/lib/rmpp-lang/50-rmpp-korean.so.off; fi
ln -sf /usr/lib/rmpp-lang/librmpp-lang.so /usr/lib/rmpp-preload.d/50-rmpp-lang.so
sync
systemctl reset-failed xochitl || true
systemctl restart xochitl
sleep 8
mount -o remount,ro / || echo "WARN: root still rw"
systemctl is-active xochitl'
