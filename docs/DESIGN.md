# rmpp-lang design

rmpp-lang is one add-on for the languages used on the reMarkable Paper Pro (OS 3.29, Qt 6.10.3). It grows out of
rmpp-korean: the Korean keyboard moves in unchanged, and fonts per language are added. Japanese and Chinese get
fonts first; their keyboards need conversion input methods and come later.

Status: prototype on branch `feature/rmpp-lang` (this document, `src/lang.cpp`, `qml/`, `tools/`).

## Goals

1. **Settings > Languages**: turn languages on and off; per language a keyboard (where one exists) and a font.
2. **Fonts per language**, including the font of typed text, installable from outside with a language tag
   (`rmpp-lang font add X.ttf --lang ko`, or `font get "Family" --lang ko` for Google Fonts OFL families).
3. **Keyboards**: Korean 2-beolsik now (on-screen, Type Folio, Bluetooth, USB; Ctrl+Space / right Alt; 한/EN
   indicator). More keyboards later (see "Next keyboards").
4. **A contract for other add-ons**: rmpp-hwr (v0.3) reads the language list and fonts from rmpp-lang.

Language codes: `ko`, `en`, `ja`, `zh`, `latin` (`latin` = the font of typed Latin text, not a language one turns on).

## Pieces

| Piece | Where | What |
|---|---|---|
| `librmpp-lang.so` | `/usr/lib/rmpp-lang/`, linked as `/usr/lib/rmpp-preload.d/50-rmpp-lang.so` | Loaded into xochitl by rmpp-preload. `lang.cpp`: settings, fonts, Settings page; `hangul.cpp`: Korean keyboard. |
| `rmpp-lang.rcc` | `/usr/lib/rmpp-lang/` | QML of the Settings page, icon, and the `ko_KR` on-screen layout (`:/misc/keyboards/ko_KR/keyboard_layout.json`). |
| `rmpp-lang` | `/usr/bin/rmpp-lang` | Tablet command (POSIX sh): languages, keyboards, fonts, probe. |
| `tools/rmpp-lang.py` | computer | Same commands over ssh; uploads font files; fetches OFL families from google/fonts. |
| `lang.conf` | `/home/root/.config/rmpp-lang/lang.conf` | Settings, `key=value`. Written by the Settings page and the command. |
| `state.json` | `/home/root/.config/rmpp-lang/state.json` | What the add-on applied (for rmpp-hwr and the command). |
| fonts | `/home/root/.local/share/fonts/rmpp-lang/<code>/*.ttf\|otf\|ttc` | One folder per language. |

The library and resources must be in `/usr/lib` (xochitl starts before `/home` is mounted); everything the user
changes lives in `/home` and survives OS updates. The add-on polls the settings file and the font folders every
1.5 s, so the command, the PC tool, a file copied over USB/ssh and the Settings page all take effect the same way.

### lang.conf

```ini
languages=ko,en          # on, in priority order (decides Han characters, see below); en is always on
keyboard.ko=1            # rmpp-lang's Korean keyboard (composition, physical-keyboard switching, indicator)
font.ko=NanumMyeongjo    # family for Korean; empty or missing = the device default
font.ja=
font.zh=
font.latin=              # typed Latin text (experimental)
latin.replaces=reMarkable Sans   # which of xochitl's document families font.latin replaces
```

### state.json (contract for rmpp-hwr)

```json
{ "version": 1,
  "languages": [
    { "code": "ko", "name": "Korean", "native": "한국어", "enabled": true, "priority": 0,
      "keyboard": true, "font": "NanumMyeongjo",
      "families": ["NanumMyeongjo", "Noto Sans CJK KR", "Noto Sans KR"],
      "files": [{ "path": "/home/root/.local/share/fonts/rmpp-lang/ko/NanumMyeongjo-Regular.ttf",
                  "families": ["NanumMyeongjo"] }] } ] }
```

`enabled` + `priority` give the language list; `font` (or the first of `families` when empty) is the font to draw
with; `files` are paths a renderer outside Qt (rmpp-hwr's ink glyphs) can open. `keyboard` is `null` where
rmpp-lang has no keyboard for the language. The file is rewritten whenever something changes; readers should
treat unknown keys as optional.

## Fonts

### Per-script fallback (works)

Qt 6.8+ has `QFontDatabase::setApplicationFallbackFontFamilies(QChar::Script, families)`. The families are tried
before the platform (fontconfig) fallbacks whenever the requested font lacks a glyph. xochitl's own fonts
("reMarkable Sans", "reMarkable Serif Small") have no Hangul, kana or Han, so for those scripts the fallback
decides what the user sees, everywhere: UI, file names, keyboard, typed text.

| Language | Scripts set (from the manifest; Korean built in) |
|---|---|
| ko | Hangul, + Han |
| ja (add-on) | Hiragana, Katakana, + Han |
| zh (add-on) | Bopomofo, + Han |
| ru (add-on) | Cyrillic (no font set: reMarkable Sans covers it) |

Han (CJK ideographs) is shared: the fonts of the enabled languages are listed in `languages` order, so Hanja in
Korean text follows the Korean font when `ko` comes first, and Japanese users can put `ja` first
(`rmpp-lang lang first ja`). Font files are registered with `QFontDatabase::addApplicationFont` (no fontconfig
cache rebuild, no restart).

Measured on the tablet (OS 3.29.1, Qt 6.10.3):

- Setting a fallback at runtime changes the UI at once (file names, Settings, the keyboard): installing
  NanumMyeongjo for `ko` turned "필기테스트" in My files into Myeongjo within the 1.5 s poll, no restart.
- Typed text on the canvas: new and edited text blocks use the fallback (Hangul, Hanja and kana in the chosen
  fonts). Text blocks of a document opened before the change, and blocks not edited since, kept their previous
  look in the test, also after a restart; xochitl seems to keep the laid-out glyphs of existing blocks. To be
  checked further (does editing a block, or a page re-render after zooming, refresh it?). The Settings page says
  "applies to new text" until this is understood.
- Qt's font cache is per thread and xochitl lays out canvas text in another thread, so the add-on applies the
  settings once at startup (before that thread exists) and then on every change; a font change can need the
  document to be reopened.

### Base font of typed Latin text (experimental)

xochitl draws typed text with "reMarkable Sans" (its UI uses the same family, "reMarkable Serif Small" for titles).
A per-script fallback cannot change Latin (the family has those glyphs), so rmpp-lang interposes Qt functions
(LD_PRELOAD wins over libQt6Gui):

- First try: `QFont::setFamily` / `QFont(family, ...)` called from xochitl's code -> the **whole UI** turned to
  the replacement (xochitl sets the application font from its own code). Not usable.
- Now: only `QTextLayout::setFont` called from xochitl's code (the canvas text layout; QML text goes through
  QtQuick and is not touched). New typed text showed in EB Garamond, the UI stayed reMarkable Sans.
- `latin.replaces` lists the families replaced (default `reMarkable Sans`); the replacement is put first with the
  original second, so missing glyphs still come from xochitl's font and the fallbacks.
- Experimental and off by default (`font.latin` empty). It depends on xochitl internals (which function lays out
  canvas text) and has to be re-checked on every OS update.

### Sync and PDF export

- **Sync**: documents store the text and its paragraph styles, not fonts. Other devices and the reMarkable apps
  show their own fonts; nothing rmpp-lang changes is uploaded. A missing font on the tablet only changes how this
  tablet draws the text.
- **PDF export** (made on the tablet by xochitl): expected to use the same layout and therefore embed the chosen
  fonts (Hangul in the Korean font, Latin in `font.latin` when set). Not tested yet; check that the exported PDF
  embeds the font subset and that line breaks match the screen. The font's license must allow embedding (OFL does).
- **Handwriting conversion and search** work on text, not glyphs: unaffected.

## Keyboards

The Korean keyboard is rmpp-korean's code (`src/hangul.cpp`) with three changes: it starts from rmpp-lang, its
layout comes from `rmpp-lang.rcc`, and `keyboard.ko=0` turns off physical-keyboard switching and the 한/EN
indicator. Which on-screen languages the globe key cycles through stays xochitl's own setting (Settings >
Language & keyboard), where Korean appears because the layout is registered.

Adding a keyboard:

- **Alphabet layouts** (one key = one character, e.g. Greek, Cyrillic): a `keyboard_layout.json` under
  `layout/misc/keyboards/<locale>/` in the qrc, an entry with `vkLayout` in `kLanguages`, and a key map for
  physical keyboards. No composition code.
- **Composition** (Korean): an input-method event filter like `hangul.cpp`.
- **Conversion IMEs** (Japanese kana-kanji, Chinese pinyin): need a candidate bar and a dictionary
  (e.g. libkkc/mozc-style data, libpinyin/rime). Out of scope for now; fonts come first.
- **Right-to-left** (Hebrew, Arabic): layout is easy, but xochitl's text editing and cursor movement must be
  checked with bidirectional text first.

### Next keyboards

On-screen layouts xochitl 3.29 already ships (locale ids in the binary): bg_BG, ca_ES, cs_CZ, da_DK, de_AT,
de_DE, el_GR, en_CA, en_GB, en_US, es_ES, es_US, et_EE, eu_ES, fi_FI, fr_CA, fr_FR, ga_IE, gl_ES, hr_HR, hu_HU,
is_IS, it_IT, lt_LT, lv_LV, nl_BE, nl_NL, no_NO, pl_PL, pt_PT, ro_RO, sk_SK, sl_SI, sv_SE. Type Folio hardware
layouts: US, UK, German, Spanish, French, Italian, Nordic.

Candidates, roughly by demand seen in public reviews and community projects (Reddit could not be searched from
here, so this ranking is weak and should be checked by hand):

| Language | Kind | Font on device | Work |
|---|---|---|---|
| Japanese, Chinese | conversion IME | Noto Sans JP / SC (+ CJK JP/SC/TC downloaded by xochitl) | large: candidate bar + dictionary |
| Russian, Ukrainian | alphabet (Cyrillic) | Noto Sans | small: layout json (bg_BG shows Cyrillic works) + physical key map |
| Turkish | alphabet (Latin + ğ ı ş) | Noto Sans, reMarkable Sans | small |
| Polish, Romanian, Croatian, Lithuanian, Latvian on the **Type Folio** (AltGr letters) | physical key map | yes | small: the on-screen layouts exist; physical AltGr mapping in the event filter |
| Hebrew, Arabic | alphabet, right-to-left | Noto Sans Hebrew / Arabic | medium: check bidi editing first |
| Vietnamese | Latin + tone composition (Telex/VNI) | Noto Sans | medium: composition filter like Hangul |

Suggested order after Korean: Cyrillic (ru, uk) and Turkish as plain layouts, Type Folio AltGr letters, then
Vietnamese (composition), then Japanese/Chinese IMEs.

## Language add-ons

The core package `rmpp-lang` contains the add-on library, the Settings page, the command, **Korean** (keyboard and
font) and the two entries every tablet has: `en` and `latin` (the typed-text font). Every other language is an
add-on: a folder in this repository, packaged as `rmpp-lang-<code>` (depends on `rmpp-lang`), chosen at install
time. Adding a language = adding a folder; the core is not rebuilt.

### In the repository

```
languages/<code>/
  language.conf            # manifest (required)
  keyboards/<layout>/keyboard_layout.json   # on-screen layouts, xochitl format (optional)
  keymap.conf              # physical-keyboard map (optional)
  fonts/                   # font files shipped as they are (optional; OFL or similar)
  fonts.conf               # fonts downloaded at package build time (optional)
  LICENSE-fonts            # license text of the fonts (required when fonts/ or fonts.conf is present)
```

`<code>`: lowercase ISO 639-1 (`ja`, `zh`, `ru`, `uk`, `tr`, `vi`, `he`, ...), a region suffix only when needed
(`zh-hant`). Reserved for the core: `ko`, `en`, `latin`.

`language.conf` (key=value, `#` comments, UTF-8; read by the core, the package build and the installer):

```ini
format=1                   # manifest format version
code=ru
name=Russian               # English name
native=Русский             # name in the language
summary_en=Russian keyboard (ЙЦУКЕН) on screen and on Type Folio, Bluetooth and USB keyboards
summary_ko=러시아어 키보드(ЙЦУКЕН): 화면 키보드와 Type Folio·블루투스·USB 키보드
version=0.1.0              # add-on version (package pkgver)
min_core=0.1.0             # oldest rmpp-lang core that understands this add-on
scripts=Cyrillic           # Qt script names whose fallback font this language decides (see below)
han=0                      # 1 = also decides Han characters, in language order (ja, zh)
input=layout               # layout | compose:<name> (built into the core: hangul, later telex) | none (fonts only)
layouts=ru_RU              # on-screen layout ids = folder names under keyboards/ ("" = none)
rtl=0                      # 1 = right-to-left (the Settings page warns that editing is not checked yet)
font=                      # default family ("" = device default); the user can pick another
```

Script names: `Latin Greek Cyrillic Armenian Hebrew Arabic Devanagari Bengali Thai Lao Georgian Hangul Hiragana
Katakana Bopomofo Han Ethiopic Khmer Mongolian Tibetan` (`QChar::Script_<name>`; unknown names are ignored).

`keymap.conf` (physical keyboards, positions of a US keyboard):

```ini
# key = character [shifted]          letters a-z, digits, and ` - = [ ] \ ; ' , . /
q = й Й
w = ц Ц
[ = х Х
# AltGr / right Alt layer (only for languages whose toggle is not right Alt)
altgr.a = ą Ą
```

`fonts.conf` (one font per line: URL, SHA-256, file name):

```
https://github.com/notofonts/noto-cjk/raw/<commit>/Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf  <sha256>  NotoSansCJKjp-Regular.otf
```

### On the tablet

| What | Path | Why there |
|---|---|---|
| Manifest, keymap, fonts | `/home/root/.local/share/rmpp-lang/languages/<code>/` (`language.conf`, `keymap.conf`, `fonts/*`) | Package files in /home survive OS updates. |
| On-screen layouts | `/usr/lib/rmpp-lang/languages/<code>.rcc` (copied by postinstall / postupgrade / reenable from `/home/root/.vellum/share/rmpp-lang-<code>/<code>.rcc`) | xochitl starts before /home is mounted; layouts must be registered at startup. The package build makes it with `rcc` from `keyboards/` (prefix `/`, files `misc/keyboards/<layout>/keyboard_layout.json`). |

The core registers every `/usr/lib/rmpp-lang/languages/*.rcc` at startup, and lists every
`/home/root/.local/share/rmpp-lang/languages/*/language.conf` (polled with the other files, so an add-on installed
or removed shows up in Settings > Languages within seconds; a new on-screen layout needs one xochitl restart,
which the package does anyway). Fonts in an add-on's `fonts/` are registered like user fonts; user fonts keep
their own folder (`/home/root/.local/share/fonts/rmpp-lang/<code>/`). A newly installed add-on is turned on
(appended to `languages=`) by its postinstall: `rmpp-lang lang on <code>`; removing it leaves `lang.conf` alone
(the core ignores codes without a manifest).

Physical keyboards: Ctrl+Space / right Alt cycle through the enabled languages that have `input` other than
`none` (with only Korean on, this is today's Korean/English toggle); the indicator shows the language's code.

### Packaging (rmpp-packages CI, proposal)

For each `languages/<code>/` of a tagged rmpp-lang release: `pkgname=rmpp-lang-<code>`, `pkgver=` the manifest
`version`, `depends="rmpp-lang>=<min_core>"`, `pkgdesc=` `summary_en`, license GPL-3.0-only plus the fonts'
license. Files: the folder minus `keyboards/` and `fonts.conf`, plus downloaded fonts (checked against SHA-256) and
`<code>.rcc`. postinstall/postupgrade/reenable copy the rcc to `/usr/lib/rmpp-lang/languages/`, run
`rmpp-lang lang on <code>` (postinstall only) and restart xochitl if a layout was added; preremove deletes the rcc.

## Settings > Languages

A sidebar row "Languages" (page id 1003; rmpp-bt 1000, rmpp-hwr 1001, rmpp-tailscale 1002).

- **List**: one row per installed language (Korean, English, typed text, then the add-ons) with a one-line
  summary ("Keyboard on · Font: NanumMyeongjo"). Tapping a row opens its page.
- **Language page**: on/off, keyboard (with where to turn on the on-screen layout), font, a sample line in that
  font, priority ("Move up": shared Han characters use the first language's font), the folder for the user's fonts.
- **Font popup**: the families for the language (the user's, the add-on's, the device's, "Default"), each shown
  in its own face; one tap picks it.

Drill-down pages follow xochitl's own Settings (Wi-Fi network details); a popup is used only for the short font
choice, where seeing the faces side by side helps. The page talks to the library through a `QQmlPropertyMap`
(`languages` JSON, `command` string); no map key is named `model`.

## Commands

```sh
rmpp-lang status
rmpp-lang lang list | lang on ja | lang off ja | lang first ja
rmpp-lang keyboard ko on|off
rmpp-lang font add NanumMyeongjo-Regular.ttf --lang ko --use
rmpp-lang font list [--lang ko] | font use "Noto Sans CJK JP" --lang ja | font rm FILE --lang ko
rmpp-lang probe [trace on|off]       # fonts in use, for debugging (/tmp/rmpp-lang-probe.txt)
python tools/rmpp-lang.py --host rmpp font get "Nanum Myeongjo" --lang ko --use    # from a computer
```

## Migration from rmpp-korean

See [MIGRATION.md](MIGRATION.md).

## Open questions

- Bold/italic of a user font: Qt picks the style from the family's files; a family with only Regular gets
  synthetic bold.
- Variable fonts (the device's Noto Sans KR is one) work as families; weights are not offered separately.
- Google Fonts by name is limited to `ofl/` (SIL OFL) families on purpose.
