# Fonts and languages

rmpp-lang gives every language on the reMarkable Paper Pro one font, used wherever that language's script appears:
typed text in notebooks, the keyboard, file names and the rest of the tablet. You pick it in
**Settings > Languages > (language) > Font**.

## Language codes

Fonts and languages are tagged with a language code: the two-letter
[ISO 639-1 code](https://en.wikipedia.org/wiki/List_of_ISO_639_language_codes) of the language, in lower case.

| Code | For |
|---|---|
| `ko` | Korean (Hangul, and Hanja in Korean text) |
| `ja` | Japanese (kana, kanji) |
| `zh` | Chinese |
| `ru`, `uk`, `el`, ... | other languages, once their add-on is installed |
| `latin` | the font of typed Latin text (experimental; replaces reMarkable's own font in notebooks only) |

`rmpp-lang lang list` on the tablet shows the codes installed there.

## Add a font

Use a `.ttf`, `.otf` or `.ttc` file whose license allows it (fonts under the SIL Open Font License are a safe choice).

**From your computer** (needs ssh access to the tablet, as for the rmpp installer):

```sh
python rmpp-lang.py --host rmpp font add MyFont-Regular.ttf MyFont-Bold.ttf --lang ko --use
python rmpp-lang.py --host rmpp font get "Nanum Myeongjo" --lang ko --use    # an OFL family from Google Fonts
```

`rmpp-lang.py` is in [tools/](../tools/rmpp-lang.py). `--use` makes it the language's font right away; without it,
choose it later in Settings.

**On the tablet** (ssh):

```sh
rmpp-lang font add /home/root/MyFont-Regular.ttf --lang ko --use
rmpp-lang font list
rmpp-lang font use "NanumMyeongjo" --lang ko       # pick an installed family; "" = back to the default
rmpp-lang font rm MyFont-Regular.ttf --lang ko
```

Or copy the files yourself into `/home/root/.local/share/fonts/rmpp-lang/<code>/` (for example `.../rmpp-lang/ko/`).
New fonts show up in Settings within a few seconds; no restart is needed.

Add the Regular file and, if the family has them, Bold, Italic and Bold Italic: bold and italic typed text use those.

## Add a language

Languages are add-ons: choose them in the rmpp installer. To create one (keyboard layout, physical-keyboard map,
fonts), add a folder `languages/<code>/` to this repository; the format is in
[DESIGN.md, "Language add-ons"](DESIGN.md#language-add-ons), and `languages/ru/` is a complete example.
