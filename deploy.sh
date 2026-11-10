#!/bin/sh
# Push build/ to the device (ssh alias rmpp) for testing, then restart xochitl.
# Never `systemctl stop xochitl`: stopping it powers the tablet off. reset-failed avoids the restart limit (4 / 10 min).
set -e
cd "$(dirname "$0")"
scp -q build/libhangul.so build/ko_KR.rcc rmpp:/home/root/
ssh rmpp 'mkdir -p /home/root/.local/share/fonts/rmpp-korean'
scp -q build/fonts/*.otf build/fonts/LICENSE rmpp:/home/root/.local/share/fonts/rmpp-korean/
ssh rmpp 'set -e
mount -o remount,rw /
mkdir -p /usr/lib/hangul
for f in libhangul.so ko_KR.rcc; do cp /home/root/$f /usr/lib/hangul/$f.new; mv /usr/lib/hangul/$f.new /usr/lib/hangul/$f; done
sync
systemctl reset-failed xochitl || true
systemctl restart xochitl
sleep 8
mount -o remount,ro / || echo "WARN: root still rw"
systemctl is-active xochitl'
