# Contributing

New keyboard languages and input methods are welcome, as an issue or as a pull request.

- **Ask for a language**: open a [language request](https://github.com/lorancecall/rmpp-lang/issues/new?template=language-request.yml).
- **Build it yourself**: open a pull request with an add-on in `languages/<code>/` (manifest `language.conf`,
  optional `keyboards/`, `keymap.conf`, `dict/`, `fonts.conf`). The format is in [docs/DESIGN.md](docs/DESIGN.md)
  ("Language add-ons"); `languages/ru/` is a small example, `languages/ko/` a full one. Build it with
  `sh tools/build-addon.sh <code>`.

## Rules for every new language

1. **Tested on a real reMarkable Paper Pro.** Install the add-on on your tablet, type in a notebook, and check the
   keyboard, the font and Settings > Languages.
2. **Screenshots made with [rmpp-studio](https://github.com/lorancecall/rmpp-studio)** (`rmpp-ctl shot out.png`):
   one of the on-screen keyboard and one of a typed sample sentence. Attach them to the issue or pull request.
3. **License.** rmpp-lang is GPL-3.0-only, and your contribution is under the same license. Word lists, dictionaries
   and fonts need a license that allows redistribution; name it and its source in `language.conf` and the README.
4. **No personal data** in files, screenshots or commit messages (names, accounts, notebook contents you don't want
   public).

The source is free to use under GPL-3.0; please keep the license and copyright notices when you reuse it.
