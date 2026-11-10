# rmpp-korean

Korean (Hangul) input, Korean fonts and Bluetooth/USB keyboard support for the reMarkable Paper Pro. Runs without xovi.

Tested on reMarkable Paper Pro, OS 3.29.1.0.

## What it does

- **On-screen keyboard:** adds a Korean 2-beolsik layout (`ko_KR`). Choose "Korean" in Settings → Language & keyboard, then switch with the globe key. Jamo are composed into syllables as you type (ㅎ ㅏ ㄴ → 한), and backspace removes one jamo at a time.
- **Physical keyboards** (Type Folio, Bluetooth, USB): **Ctrl+Space** or **right Alt / right Cmd** switches between English and Korean. Letters are mapped to 2-beolsik and composed the same way.
- **Fonts:** installs Noto Sans CJK KR Regular and Bold (SIL OFL 1.1) in `/home/root/.local/share/fonts/rmpp-korean`. The device already ships Noto Sans KR.

For a Bluetooth settings page and quick-settings toggle, see [rmpp-bt](https://github.com/lorancecall/rmpp-bt).

## How it works

`libhangul.so` is loaded into xochitl with `LD_PRELOAD` (a systemd drop-in). It registers `ko_KR.rcc`, which adds `:/misc/keyboards/ko_KR/keyboard_layout.json` next to the built-in layouts. An application event filter then rewrites single-jamo input-method commits into composed syllables, using replace-previous-character commits.

xochitl starts before `/home` is mounted, so the library and layout live in `/usr/lib/hangul`. The root file system is read-only and is replaced by OS updates, so the installer has to run again after an update.

## Build

Requires an aarch64 cross toolchain (Arm GNU `aarch64-none-linux-gnu`, GCC 14), Qt 6.10 headers, the device's `libQt6Core.so.6` and `libQt6Gui.so.6`, and `pyside6-rcc`.

```sh
SDK=/path/to/sdk sh build.sh     # build/libhangul.so, build/ko_KR.rcc, build/fonts/
sh deploy.sh                     # test install over ssh (host alias "rmpp")
```

`deploy.sh` is for development. Never run `systemctl stop xochitl` on the device: stopping it powers the tablet off.

## Status

- [x] On-screen Korean layout and composition
- [x] Physical keyboard toggle (Ctrl+Space, right Alt)
- [x] Fonts
- [ ] Hide on-screen keys while a Bluetooth keyboard is connected (keep the format bar)
- [ ] On-screen indicator when the input language changes
- [ ] Vellum package (install, remove, `vellum reenable` after OS updates)
