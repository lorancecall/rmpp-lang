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

| Language | Scripts set |
|---|---|
| ko | Hangul, + Han |
| ja | Hiragana, Katakana, + Han |
| zh | Bopomofo, + Han |

Han (CJK ideographs) is shared: the fonts of the enabled languages are listed in `languages` order, so Hanja in
Korean text follows the Korean font when `ko` comes first, and Japanese users can put `ja` first
(`rmpp-lang lang first ja`). Font files are registered with `QFontDatabase::addApplicationFont` (no fontconfig
cache rebuild, no restart).

FINDINGS_FALLBACK

### Base font of typed Latin text (experimental)

FINDINGS_LATIN

### Sync and PDF export

FINDINGS_EXPORT

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

## Settings > Languages

A sidebar row "Languages" (page id 1003; rmpp-bt 1000, rmpp-hwr 1001, rmpp-tailscale 1002) placed after
xochitl's own language row. Per language: on/off, keyboard (Korean), font (cycles through the language's
installed families and the device's, with a sample line in that font), the folder to drop fonts into. The page
talks to the library through a `QQmlPropertyMap` (`languages` JSON, `command` string); no map key is named `model`.

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
