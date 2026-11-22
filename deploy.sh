#!/bin/sh
# Development install over ssh (default host alias "rmpp"), then restart xochitl.
#   sh deploy.sh [HOST]            install build/ the way the package does (rmpp-preload's one-file layout:
#                                  everything in /home, nothing on the root file system)
#   sh deploy.sh [HOST] --remove   take a hand-made install out again (not for Vellum installs: use vellum)
#   ADDONS="ko ja" sh deploy.sh    language add-ons to install (default: ko; built with tools/build-addon.sh)
# Needs rmpp-preload 0.2.0 or later (its launcher reads preload.d/ and env.d/ in /home).
# Over a Vellum install this replaces the package's files; `vellum fix` (or the next upgrade) puts them back.
# Never `systemctl stop xochitl`: stopping it powers the tablet off. reset-failed avoids the restart limit (4 / 10 min).
set -e
cd "$(dirname "$0")"
HOST=${1:-rmpp}
ADDONS=${ADDONS-ko}
S=/home/root/.vellum/share/rmpp-lang
P=/home/root/.vellum/share/rmpp-preload
if [ "$2" = "--remove" ]; then
  ssh "$HOST" "S=$S P=$P"'; set -e
rm -f $P/preload.d/50-rmpp-lang.so $P/env.d/rmpp-lang.env /home/root/.vellum/bin/rmpp-lang
rm -rf $S
systemctl reset-failed xochitl || true; systemctl restart xochitl; sleep 8
systemctl is-active xochitl'
  exit 0
fi
ssh "$HOST" "[ -x $P/launch ]" || { echo "rmpp-preload 0.2.0 (launcher in $P) is not installed on $HOST" >&2; exit 1; }
ssh "$HOST" "mkdir -p $S/languages"
# .new then mv: the running xochitl keeps the old library mapped
scp -q build/librmpp-lang.so "$HOST":$S/librmpp-lang.so.new
scp -q build/rmpp-lang.rcc "$HOST":$S/rmpp-lang.rcc.new
scp -q build/rmpp-lang "$HOST":$S/rmpp-lang.new
for code in $ADDONS; do
  [ -f build/languages/$code/language.conf ] && [ build/languages/$code/language.conf -nt languages/$code/language.conf ] || sh tools/build-addon.sh "$code"
  ssh "$HOST" "rm -rf /home/root/.local/share/rmpp-lang/languages/$code; mkdir -p /home/root/.local/share/rmpp-lang/languages"
  scp -q -r build/languages/$code "$HOST":/home/root/.local/share/rmpp-lang/languages/
  [ -f build/languages/$code.rcc ] && scp -q build/languages/$code.rcc "$HOST":$S/languages/$code.rcc.new
done
ssh "$HOST" "S=$S P=$P ADDONS='$ADDONS'"'; set -e
for f in librmpp-lang.so rmpp-lang.rcc rmpp-lang; do mv $S/$f.new $S/$f; done
chmod 755 $S/librmpp-lang.so $S/rmpp-lang
for code in $ADDONS; do [ -f $S/languages/$code.rcc.new ] && mv $S/languages/$code.rcc.new $S/languages/$code.rcc; done
mkdir -p $P/preload.d $P/env.d /home/root/.vellum/bin
ln -sfn ../../rmpp-lang/librmpp-lang.so $P/preload.d/50-rmpp-lang.so
echo "export RMPPLANG_RCC=$S/rmpp-lang.rcc" > $P/env.d/rmpp-lang.env
ln -sfn ../share/rmpp-lang/rmpp-lang /home/root/.vellum/bin/rmpp-lang
for code in $ADDONS; do $S/rmpp-lang lang on $code >/dev/null; done
sync
systemctl reset-failed xochitl || true
systemctl restart xochitl
sleep 8
systemctl is-active xochitl'
