# rmpp-lang design

rmpp-lang is one add-on for the languages used on the reMarkable Paper Pro (OS 3.29, Qt 6.10.3). It grows out of
rmpp-korean. The core is language-neutral (English and the typed-text font only); every language, Korean
included, is an add-on in `languages/<code>/` that the user picks when installing. The Korean keyboard moves in
unchanged (its composition engine lives in the core, the add-on turns it on). Japanese and Chinese get fonts first;
their keyboards need conversion input methods and come later.

Status: prototype on branch `feature/rmpp-lang` (this document, `src/lang.cpp`, `qml/`, `tools/`).

## Goals

1. **Settings > Languages**: turn languages on and off; per language a keyboard (where one exists) and a font.
2. **Fonts per language**, including the font of typed text, installable from outside with a language tag
   (`rmpp-lang font add X.ttf --lang ko`, or `font get "Family" --lang ko` for Google Fonts OFL families).
3. **Keyboards**: Korean 2-beolsik (on-screen, Type Folio, Bluetooth, USB; Ctrl+Space / right Alt; 한/EN
   indicator) and Russian ЙЦУКЕН as add-ons; more later (see "Next keyboards").
4. **A contract for other add-ons**: rmpp-hwr (v0.3) reads the language list and fonts from rmpp-lang.

Language codes: `en` and `latin` in the core (`latin` = the font of typed Latin text, not a language one turns
on); add-ons `ko`, `ja`, `zh`, `ru`, ... (rmpp-hwr uses the same codes).

## Pieces

| Piece | Where | What |
|---|---|---|
| `librmpp-lang.so` | `/usr/lib/rmpp-lang/`, linked as `/usr/lib/rmpp-preload.d/50-rmpp-lang.so` | Loaded into xochitl by rmpp-preload. `lang.cpp`: settings, fonts, add-ons, Settings page; `hangul.cpp`: Hangul composition (used by the ko add-on). |
| `rmpp-lang.rcc` | `/usr/lib/rmpp-lang/` | QML of the Settings page and its icon. (On-screen layouts come with the add-ons.) |
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
languages=ko,en          # on, in order (keyboard switching order; also decides Han characters); en is always on;
                         # default "en", each add-on's postinstall adds itself (rmpp-lang lang on <code>)
keyboard.ko=1            # the language's keyboard (0 = fonts only; physical switching and indicator skip it)
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
      "families": ["NanumMyeongjo", "Noto Sans KR"],
      "files": [{ "path": "/home/root/.local/share/fonts/rmpp-lang/ko/NanumMyeongjo-Regular.ttf",
                  "families": ["NanumMyeongjo"] }] } ] }
```

`enabled` + `priority` give the language list; `font` (or the first of `families` when empty) is the font to draw
with; `files` are paths a renderer outside Qt (rmpp-hwr's ink glyphs) can open. `keyboard` is `null` where
rmpp-lang has no keyboard for the language. The file is rewritten whenever something changes; readers should
treat unknown keys as optional.

### fonts.json (one font per language, for drawing outside Qt)

The developer's decision: each language has one font, chosen centrally in rmpp-lang; other add-ons (rmpp-hwr's
Convert to ink) draw with it and offer no font choice of their own.
`/home/root/.config/rmpp-lang/fonts.json`, rewritten with state.json:

```json
{ "version": 1,
  "languages": {
    "ko":    { "family": "Noto Sans KR", "file": "/usr/share/fonts/ttf/noto/NotoSansKR-VariableFont_wght.ttf", "chosen": "" },
    "ja":    { "family": "Noto Sans JP", "file": "/usr/share/fonts/ttf/noto/NotoSansJP-VariableFont_wght.ttf", "chosen": "" },
    "en":    { "family": "Noto Sans", "file": "/usr/share/fonts/ttf/noto/NotoSans-VariableFont_wdth,wght.ttf", "chosen": "" },
    "latin": { "family": "Noto Sans", "file": "/usr/share/fonts/ttf/noto/NotoSans-VariableFont_wdth,wght.ttf", "chosen": "" } } }
```

- `chosen`: the user's choice (`""` = default). `family` / `file`: what is actually used, always a readable file.
- The file is a font rmpp-lang registered (the "Regular" one of the family when it has several), else fontconfig's
  match (`fc-match "<family>:style=Regular"`). xochitl's own "reMarkable Sans" (en/latin default) is not a file:
  fontconfig's substitute is given and `family` names it. Files may be variable fonts or .ttc collections.
- Every installed language has an entry (enabled or not); use state.json for which are on.
- Fonts outside rmpp-lang's folders (the OS's `/usr/share/fonts`, fonts a user put anywhere fontconfig looks, e.g.
  `/home/root/.local/share/fonts/<any>/`) are offered and used but never moved or deleted by rmpp-lang.

## Fonts

### Per-script fallback (works)

Qt 6.8+ has `QFontDatabase::setApplicationFallbackFontFamilies(QChar::Script, families)`. The families are tried
before the platform (fontconfig) fallbacks whenever the requested font lacks a glyph. xochitl's own fonts
("reMarkable Sans", "reMarkable Serif Small") have no Hangul, kana or Han, so for those scripts the fallback
decides what the user sees, everywhere: UI, file names, keyboard, typed text.

| Language | Scripts set (from the add-on's manifest) |
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

The Korean keyboard is rmpp-korean's code: the Hangul composition engine (`src/hangul.cpp`) stays in the core
and is active while the input language's manifest says `input=compose:hangul` (`languages/ko`); the `ko_KR`
on-screen layout comes with the add-on (`ko.rcc`), the 한 label from its `label=`. `keyboard.ko=0` turns off
physical-keyboard switching and the indicator for it. Which on-screen languages the globe key cycles through
stays xochitl's own setting (Settings > Language & keyboard), where Korean appears because the layout is registered.

Adding a keyboard:

- **Alphabet layouts** (one key = one character, e.g. Greek, Cyrillic): an add-on with
  `keyboards/<locale>/keyboard_layout.json` and `keymap.conf` (see `languages/ru`). No code.
- **Composition** (Korean, later Vietnamese Telex): an engine in the core (`hangul.cpp`), chosen by the add-on's
  `input=compose:<name>`.
- **Conversion IMEs** (Japanese kana-kanji, Chinese pinyin): need a candidate bar and a dictionary
  (e.g. libkkc/mozc-style data, libpinyin/rime). Out of scope for now; fonts come first.
- **Conversion in a composing language** (Korean Hangul -> Hanja): the add-on's `convert=` and dictionary,
  the candidate bar of the core (`hanja.cpp`); see "Hanja conversion" below.
- **Right-to-left** (Hebrew, Arabic): layout is easy, but xochitl's text editing and cursor movement must be
  checked with bidirectional text first.

### Hanja conversion

- **Trigger**: the 漢 button on the keyboard's format bar (left of the language indicator, shown while the input
  language has `convert=hanja`), or the Hanja key / F9 of a physical keyboard.
- **What is converted**: the selection if there is one (a word of up to 8 syllables), else the Hangul syllables
  right before the cursor: candidates for the longest run first (한국어 -> 韓國語), then shorter ones down to the
  last syllable (語 for 어), as in the usual Korean IMEs. Text comes from the field's
  `ImSurroundingText` / `ImCursorPosition` (the Hangul engine already relies on them).
- **Candidate bar**: over the format bar, Hanja with its meaning ("옳을 가"), numbered; ‹ › page. Physical keys
  while it is open: 1-9 pick, Left/Right/Space/Tab page, Esc closes, any other key closes it and types.
- **Replacement**: an input-method commit replacing the syllables before the cursor (`setCommitString(hanja,
  -len, len)`) or the selection; the Hangul engine sees a non-jamo commit and ends its syllable.
- **Dictionary**: `languages/ko/dict/hanja.txt` from libhangul's `data/hanja/hanja.txt` (BSD-3-Clause, header
  kept), rebuilt with `tools/hanja-dict.py`: `reading:hanja:meaning`, sorted by the reading's UTF-8 bytes; within a
  reading, entries with a meaning first (韓國 before 寒國). 303k entries, 6.4 MB; the core memory-maps it and
  binary-searches, so xochitl's memory does not grow.
- **Later**: learn the user's picks (move them first), Hanja -> Hangul back, the same bar for ja/zh IMEs.

### Next keyboards

On-screen layouts xochitl 3.29 already ships (locale ids in the binary): bg_BG, ca_ES, cs_CZ, da_DK, de_AT,
de_DE, el_GR, en_CA, en_GB, en_US, es_ES, es_US, et_EE, eu_ES, fi_FI, fr_CA, fr_FR, ga_IE, gl_ES, hr_HR, hu_HU,
is_IS, it_IT, lt_LT, lv_LV, nl_BE, nl_NL, no_NO, pl_PL, pt_PT, ro_RO, sk_SK, sl_SI, sv_SE. Type Folio hardware
layouts: US, UK, German, Spanish, French, Italian, Nordic.

Candidates, roughly by demand seen in public reviews and community projects (Reddit could not be searched from
here, so this ranking is weak and should be checked by hand):

| Language | Kind | Font on device | Work |
|---|---|---|---|
| Japanese, Chinese | conversion IME | Noto Sans JP / SC (OS, variable) | large: candidate bar + dictionary |
| Russian, Ukrainian | alphabet (Cyrillic) | Noto Sans | small: layout json (bg_BG shows Cyrillic works) + physical key map |
| Turkish | alphabet (Latin + ğ ı ş) | Noto Sans, reMarkable Sans | small |
| Polish, Romanian, Croatian, Lithuanian, Latvian on the **Type Folio** (AltGr letters) | physical key map | yes | small: the on-screen layouts exist; physical AltGr mapping in the event filter |
| Hebrew, Arabic | alphabet, right-to-left | Noto Sans Hebrew / Arabic | medium: check bidi editing first |
| Vietnamese | Latin + tone composition (Telex/VNI) | Noto Sans | medium: composition filter like Hangul |

Suggested order after Korean: Cyrillic (ru, uk) and Turkish as plain layouts, Type Folio AltGr letters, then
Vietnamese (composition), then Japanese/Chinese IMEs.

## Language add-ons

The core package `rmpp-lang` contains the library (with the composition engines), the Settings page, the command
and the two entries every tablet has: `en` and `latin` (the typed-text font). Every language, Korean included, is
an add-on: a folder in this repository, packaged as `rmpp-lang-<code>` (depends on `rmpp-lang`), chosen at install
time (`default=1` preselects it; Korean has it). Adding a language = adding a folder; the core is not rebuilt.

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
(`zh-hant`). Reserved for the core: `en`, `latin`. The code set is shared with rmpp-hwr and the rmpp-hwr-db dataset:
ISO 639-1 plus the variants listed in rmpp-hwr-db `scripts/languages.py` (`VARIANTS`, now `zh-hant`); a new
variant is added there first.

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
families=Noto Sans         # device families worth offering besides the add-on's fonts (comma-separated)
default=0                  # 1 = preselected in the installer menu
label=                     # indicator text on the format bar ("" = the code in capitals; ko: 한)
keyboard_name=ЙЦУКЕН       # how Settings names the layout ("" = the layout ids)
sample=Здравствуйте, хорошего дня · 123   # Settings sample line in the chosen font
sample_plain=              # shown instead for fonts that cannot render `sample` (missing glyphs, uncomposed old
                           # Hangul jamo); ko: Hunminjeongeum in old Hangul, plain = the same line in modern Korean
convert=                   # conversion offered by the core: hanja ("" = none); ko: hanja
convert_dict=              # its dictionary, relative to the add-on folder; ko: dict/hanja.txt
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
`none` (with only Korean on, this is today's Korean/English toggle); the indicator shows the language's label.

### Packaging (rmpp-packages CI, proposal)

For each `languages/<code>/` of a tagged rmpp-lang release: `pkgname=rmpp-lang-<code>`, `pkgver=` the manifest
`version`, `depends="rmpp-lang>=<min_core>"`, `pkgdesc=` `summary_en`, license GPL-3.0-only plus the fonts'
license. Files: the folder minus `keyboards/` and `fonts.conf`, plus downloaded fonts (checked against SHA-256) and
`<code>.rcc`. postinstall/postupgrade/reenable copy the rcc to `/usr/lib/rmpp-lang/languages/`, run
`rmpp-lang lang on <code>` (postinstall only) and restart xochitl if a layout was added; preremove deletes the rcc.

## Settings > Languages

A sidebar row "Languages" (page id 1003; rmpp-bt 1000, rmpp-hwr 1001, rmpp-tailscale 1002).

- **List**: the languages in use in their order (▲ ▼: the order keyboards switch in; the same order decides
  which font draws Han characters shared by ko/ja/zh), then the others; one row per installed language (the add-ons, English, typed text) with a one-line
  summary ("Keyboard on · Font: NanumMyeongjo"). Tapping a row opens its page.
- **Language page**: on/off, keyboard (with where to turn on the on-screen layout), font, a sample line in that
  font, priority ("Move up": shared Han characters use the first language's font), the folder for the user's fonts.
- **Font popup**: the families for the language (the user's, the add-on's, the device's), each shown in its own
  face; the language's default family comes first, marked "· default" (picking it stores `font.<code>=` empty,
  i.e. follow the default); one tap picks a font. Fonts are added and removed outside Settings (decided: no
  remove button): with `rmpp-lang font add|rm` on the tablet or `tools/rmpp-lang.py` from a computer.

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

## From rmpp-korean

No migration: rmpp-korean is removed and rmpp-lang installed fresh; release order in [MIGRATION.md](MIGRATION.md).

## Open questions

- Bold/italic of a user font: Qt picks the style from the family's files; a family with only Regular gets
  synthetic bold.
- Variable fonts (the device's Noto Sans KR is one) work as families; weights are not offered separately.
- Google Fonts by name is limited to `ofl/` (SIL OFL) families on purpose.
- Chinese (left to whoever builds the Chinese add-on): the OS ships only Noto Sans SC, so Traditional characters
  are drawn with Simplified glyph shapes. A Traditional variant (`zh-hant` add-on with Noto Sans TC / CJK TC via
  `fonts.conf`) and a sample line whose characters differ between SC and TC (e.g. 骨 直 角 説 說) would make the
  difference visible in Settings.
