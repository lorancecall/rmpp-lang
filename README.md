# rmpp-lang

Languages for the reMarkable Paper Pro: a keyboard and a font for each language, on the on-screen keyboard and on
Type Folio, Bluetooth and USB keyboards. Runs without xovi and keeps working after a reboot.

Tested on reMarkable Paper Pro, OS 3.29.1.

rmpp-lang is a small core plus one add-on per language. You pick the languages in the installer:

| Add-on | Keyboard | Font (default) |
|---|---|---|
| `ko` Korean | 2-beolsik on screen; Hangul composed from physical keys too | Noto Sans KR |
| `ru` Russian | ЙЦУКЕН on screen and on physical keyboards | reMarkable's own (covers Cyrillic) |
| `ja` Japanese | typed in Hangul on the Korean keyboard (도쿄 -> 東京), needs `ko` | Noto Sans JP |
| `zh` Chinese | typed in Hangul on the Korean keyboard (중궈 -> 中国), needs `ko` | Noto Sans SC |

English is always there. More languages are welcome: see [Add a language](docs/FONTS.md#add-a-language).

## Install

Use the installer from [rmpp-packages](https://github.com/lorancecall/rmpp-packages). It installs Vellum if needed,
then rmpp-lang with the languages you select (Korean is preselected).

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

You need developer mode turned on. The installer asks for the tablet's root password once (Settings > General >
Help > About > Copyrights and licenses). If you already use Vellum, add the repository and run
`vellum add rmpp-lang rmpp-lang-ko` (one `rmpp-lang-<code>` per language). After a reMarkable OS update, run the
installer again (or `vellum reenable`).

## Use

- **Settings > Languages** (above Handwriting): the installed languages in keyboard order (▲▼ to reorder), and a
  page per language to turn its keyboard on or off and choose its font, with a sample line.
- **On-screen keyboard:** turn the language on in Settings > Language & keyboard, then switch with the globe key.
  Korean jamo join into syllables as you type (ㅎ ㅏ ㄴ → 한); backspace removes one jamo at a time; Shift gives
  ㅃ ㅉ ㄸ ㄲ ㅆ ㅒ ㅖ.
- **Type Folio, Bluetooth and USB keyboards:** **Ctrl+Space** or **right Alt / right Cmd** cycles through English
  and the languages with a keyboard, in Settings order. The on-screen keyboard follows.
- **Hanja (Korean):** tap **漢** on the format bar, or press the Hanja key or **F9**, to turn the Hangul before
  the cursor (or the selected word) into Hanja: pick from the candidates (1-9 on a physical keyboard).
- **Japanese and Chinese in Hangul:** turn on the keyboard of Japanese or Chinese in Settings > Languages, switch
  to it (日 / 中), type the sound in Hangul and tap **変** / **汉** (or Hanja / F9): 니혼고 -> 日本語,
  다베마스 -> 食べます, 베이징 -> 北京, 니하오 -> 你好.
- **Indicator:** a button on the format bar above the keyboard shows the current language (EN, 한, RU, ...); tap
  it to switch.
- **External keyboards:** while a Bluetooth or USB keyboard is connected, the on-screen keys fold away and the
  format bar stays. With [rmpp-bt](https://github.com/lorancecall/rmpp-bt) you can set a keyboard to **Type Folio
  mode** instead.

## Fonts

Each language uses one font everywhere its script appears: typed text, the keyboard, file names and the rest of
the tablet. Fonts change at once, without a restart. The defaults are the Noto fonts that come with the tablet.

Add your own (`.ttf`, `.otf`, `.ttc`) from your computer with [tools/rmpp-lang.py](tools/rmpp-lang.py), or on the
tablet:

```sh
rmpp-lang font add /home/root/MyFont-Regular.ttf --lang ko --use
rmpp-lang font list
```

Full guide with language codes: **[docs/FONTS.md](docs/FONTS.md)**.

## For other add-ons

rmpp-lang writes `/home/root/.config/rmpp-lang/state.json` (languages, order, keyboards, fonts) and `fonts.json`
(one family and a readable font file per language), so other add-ons can follow the user's languages and fonts;
[rmpp-hwr](https://github.com/lorancecall/rmpp-hwr) draws converted ink with them. Format: [docs/DESIGN.md](docs/DESIGN.md).

## How it works

`librmpp-lang.so` is loaded into xochitl at boot by `rmpp-preload` (a launcher that adds
`/usr/lib/rmpp-preload.d/*.so` to `LD_PRELOAD`, after xovi's if xovi is in use). It finds the language add-ons in
`/home/root/.local/share/rmpp-lang/languages/`, registers their on-screen layouts (`<code>.rcc`) next to the
built-in ones, sets one fallback font per script with `QFontDatabase::setApplicationFallbackFontFamilies`, maps
physical-keyboard keys per language, and composes Hangul for add-ons with `input=compose:hangul`. The Settings page
is QML from `rmpp-lang.rcc`.

xochitl starts before `/home` is mounted, so the library and the layouts live in `/usr/lib/rmpp-lang`. The root
file system is read-only and an OS update replaces it, which is why the packages restore these files with
`vellum reenable`. Design, add-on format and tablet findings: [docs/DESIGN.md](docs/DESIGN.md).

## Build

Requires an aarch64 cross toolchain (Arm GNU `aarch64-none-linux-gnu`, GCC 14), Qt 6.10 headers (QtCore, QtGui,
QtQml, QtQuick), the device's Qt libraries to link against, and `pyside6-rcc`.

```sh
SDK=/path/to/sdk sh build.sh           # build/librmpp-lang.so, build/rmpp-lang.rcc, build/rmpp-lang
sh tools/build-addon.sh ko ru          # build/languages/<code>/ and build/languages/<code>.rcc
ADDONS="ko ru" sh deploy.sh rmpp       # development install over ssh (host alias "rmpp")
sh deploy.sh rmpp --remove             # remove it again
```

Never run `systemctl stop xochitl` on the device: stopping it powers the tablet off. More than four xochitl
restarts in ten minutes reboots it; `deploy.sh` runs `systemctl reset-failed xochitl` first.

## History

rmpp-lang grew out of rmpp-korean 0.1.0 (Korean keyboard and fonts). There is no upgrade path: remove rmpp-korean
(`vellum del rmpp-korean`) and install rmpp-lang with `rmpp-lang-ko`. See [docs/MIGRATION.md](docs/MIGRATION.md).

## License

GPL-3.0-only. See [LICENSE](LICENSE). Fonts you add keep their own licenses. The Hanja dictionary
(`languages/ko/dict/hanja.txt`) comes from [libhangul](https://github.com/libhangul/libhangul) under the
BSD-3-Clause license in its header. The Japanese dictionary (`languages/ja/dict/ja.txt`) comes from
[SKK-JISYO.L](https://github.com/skk-dev/dict) (GPL-2.0-or-later); the Chinese one (`languages/zh/dict/zh.txt`)
from [rime](https://github.com/rime) luna_pinyin and essay (LGPL-3.0) with [OpenCC](https://github.com/BYVoid/OpenCC)
tables (Apache-2.0).
