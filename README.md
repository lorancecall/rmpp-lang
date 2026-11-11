# rmpp-korean

> **Becoming rmpp-lang** (branch `feature/rmpp-lang`): one add-on for the tablet's languages, Korean built in,
> other languages as add-ons in `languages/`, fonts per language. See [docs/DESIGN.md](docs/DESIGN.md) and
> [docs/MIGRATION.md](docs/MIGRATION.md). The text below describes the released rmpp-korean 0.1.0.

## Fonts

Each language uses one font, chosen in **Settings > Languages**. To add your own fonts for a language (language
codes, commands from your computer or on the tablet), see **[docs/FONTS.md](docs/FONTS.md)**.

Korean (Hangul) input and Korean fonts for the reMarkable Paper Pro, on the on-screen keyboard and on Type Folio, Bluetooth and USB keyboards. Runs without xovi and keeps working after a reboot.

Tested on reMarkable Paper Pro, OS 3.29.1.0.

## Install

Use the installer from [rmpp-packages](https://github.com/lorancecall/rmpp-packages). It installs Vellum if needed, then this package (and [rmpp-bt](https://github.com/lorancecall/rmpp-bt) for Bluetooth).

**Windows (PowerShell):**

```powershell
irm https://lorancecall.github.io/rmpp-packages/install-rmpp.ps1 -OutFile install-rmpp.ps1
powershell -ExecutionPolicy Bypass -File install-rmpp.ps1
```

**Linux / macOS:**

```sh
curl -fsSL https://lorancecall.github.io/rmpp-packages/install-rmpp.sh -o install-rmpp.sh
sh install-rmpp.sh
```

You need developer mode turned on. The installer asks for the tablet's root password once (Settings > General > Help > About > Copyrights and licenses). If you already use Vellum, add the repository and run `vellum add rmpp-korean` instead. After a reMarkable OS update, run the installer again (or `vellum reenable`).

## Use

- **On-screen keyboard:** in Settings > Language & keyboard, turn on **Korean**, then switch with the globe key. Jamo join into syllables as you type (ㅎ ㅏ ㄴ → 한), and backspace removes one jamo at a time. The layout is 2-beolsik; Shift gives ㅃ ㅉ ㄸ ㄲ ㅆ ㅒ ㅖ.
- **Type Folio, Bluetooth and USB keyboards:** **Ctrl+Space** or **right Alt / right Cmd** switches between English and Korean. The on-screen keyboard follows the same language.
- **Indicator:** a **한 / EN** button on the format bar above the keyboard shows the current language; tap it to switch.
- **External keyboards:** while a Bluetooth or USB keyboard is connected, the on-screen keys fold away and the format bar stays. With rmpp-bt you can set a keyboard to **Type Folio mode** instead (the typing layout and format button of a real Type Folio).
- **Fonts:** Noto Sans CJK KR Regular and Bold (SIL OFL 1.1) in `/home/root/.local/share/fonts/rmpp-korean`. The device already ships Noto Sans KR.

## How it works

`libhangul.so` is loaded into xochitl at boot by `rmpp-preload` (a launcher that adds `/usr/lib/rmpp-preload.d/*.so` to `LD_PRELOAD`, after xovi's if xovi is in use). It registers `ko_KR.rcc`, which adds `:/misc/keyboards/ko_KR/keyboard_layout.json` next to the built-in layouts. An application event filter rewrites single-jamo input-method commits into composed syllables with replace-previous-character commits, and maps physical-keyboard letters to jamo in Korean mode.

xochitl starts before `/home` is mounted, so the library and layout live in `/usr/lib/rmpp-korean`. The root file system is read-only and an OS update replaces it, which is why the package restores these files with `vellum reenable`.

## Build

Requires an aarch64 cross toolchain (Arm GNU `aarch64-none-linux-gnu`, GCC 14), Qt 6.10 headers (QtCore, QtGui, QtQml, QtQuick), the device's Qt libraries to link against, and `pyside6-rcc`.

```sh
SDK=/path/to/sdk sh build.sh     # build/librmpp-lang.so, build/rmpp-lang.rcc, build/rmpp-lang, build/fonts/
sh tools/build-addon.sh ru       # build/languages/ru/ and build/languages/ru.rcc
ADDONS="ja ru" sh deploy.sh rmpp # development install over ssh (host alias "rmpp")
```

Never run `systemctl stop xochitl` on the device: stopping it powers the tablet off. More than four xochitl restarts in ten minutes reboots it; `deploy.sh` runs `systemctl reset-failed xochitl` first.

## Status

- [x] On-screen Korean layout and composition
- [x] Physical keyboard toggle (Ctrl+Space, right Alt) synced with the on-screen keyboard
- [x] 한 / EN indicator on the format bar
- [x] Fonts
- [x] Hide on-screen keys while an external keyboard is connected; per-keyboard Type Folio mode (with rmpp-bt)
- [x] Vellum package ([rmpp-packages](https://github.com/lorancecall/rmpp-packages))
- [ ] Indicator in Type Folio mode
- [ ] Handwriting recognition for Korean

## License

GPL-3.0-only. See [LICENSE](LICENSE). The fonts are under the SIL Open Font License 1.1.
