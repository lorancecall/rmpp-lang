# Migration: rmpp-korean -> rmpp-lang

Proposal, to agree with whoever maintains the package repository and installer before anything public changes.
Nothing here is done by the `feature/rmpp-lang` branch itself.

rmpp-korean becomes two things: the language-neutral core `rmpp-lang` and the Korean add-on `rmpp-lang-ko`
(keyboard, fonts). Existing rmpp-korean users get both.

## Order

1. **Merge this branch** (after review) into `main` of `lorancecall/rmpp-korean`.
2. **Rename the repository** `lorancecall/rmpp-korean` -> `lorancecall/rmpp-lang`. GitHub redirects the old URLs
   (web, git, release downloads), so the rmpp-korean 0.1.0 package keeps building and existing links keep working.
   Update the README title and links in the same step (README is left as rmpp-korean on this branch on purpose).
3. **Release v0.1.0 of rmpp-lang** with the core assets `librmpp-lang.so`, `rmpp-lang.rcc`, `rmpp-lang` (tablet
   command) and `LICENSE`. Add-ons are built from `languages/<code>/` of the same tag (`tools/build-addon.sh`
   shows the result); fonts listed in `fonts.conf` are downloaded at build time and checked against SHA-256.
4. **Packages** (rmpp-packages):
   - `packages/rmpp-lang/VELBUILD` (0.1.0-r0): the core, `replaces="rmpp-korean"`. No fonts, no language.
   - `packages/rmpp-lang-<code>/` generated from `languages/<code>/`, `rmpp-lang-ko` among them (fonts
     Noto Sans CJK KR Regular/Bold, `ko.rcc`); its postinstall runs `rmpp-lang lang on ko`.
   - `packages/rmpp-korean` 0.2.0-r0 becomes an empty transitional package: `depends="rmpp-lang rmpp-lang-ko"`, no
     files. `vellum upgrade` then moves every rmpp-korean user to rmpp-lang + Korean without a manual step; it can
     be dropped from the index later.
5. **Installer**: `mods.txt` entry `rmpp-korean` -> `rmpp-lang` (keep `default=1`), `rmpp-mod.conf` from this
   branch (`addons=` list; languages shown indented under rmpp-lang, those with `default=1` preselected: Korean).
6. **rmpp-hwr v0.3** reads `/home/root/.config/rmpp-lang/state.json` (format in DESIGN.md) for languages and fonts,
   and falls back to its own defaults when the file is missing.

## rmpp-lang (core) package

| Package file | Copied to (postinstall / postupgrade / reenable) |
|---|---|
| `/home/root/.vellum/share/rmpp-lang/librmpp-lang.so` | `/usr/lib/rmpp-lang/librmpp-lang.so` |
| `/home/root/.vellum/share/rmpp-lang/rmpp-lang.rcc` | `/usr/lib/rmpp-lang/rmpp-lang.rcc` |
| `/home/root/.vellum/share/rmpp-lang/rmpp-lang` | `/usr/bin/rmpp-lang` (755) |

postinstall / postupgrade (root remounted rw as now):

```sh
mkdir -p /usr/lib/rmpp-lang/languages
ln -sf /usr/lib/rmpp-lang/librmpp-lang.so /usr/lib/rmpp-preload.d/50-rmpp-lang.so
# rmpp-korean 0.1.0: both libraries would compose Hangul twice
rm -f /usr/lib/rmpp-preload.d/50-rmpp-korean.so
rm -rf /usr/lib/rmpp-korean
```

then `reset-failed` + `restart xochitl` as now. Without a `lang.conf` the core starts with English only; each
add-on's postinstall turns its language on. preremove: remove the link, `/usr/lib/rmpp-lang`, `/usr/bin/rmpp-lang`;
keep `/home/root/.config/rmpp-lang` and user fonts (a reinstall finds them again).

## rmpp-lang-ko and what "keeping settings" covers

- `rmpp-lang lang on ko` in its postinstall: Korean keyboard on, as with rmpp-korean.
- Which on-screen languages are on (Korean in Language & keyboard) is xochitl's setting: untouched. The layout id
  stays `ko_KR` (now from `ko.rcc`), so Korean stays selected.
- rmpp-bt's per-keyboard Type Folio mode (`/home/root/.config/rmpp-bt/keyboards.conf`) is read the same way.
- Fonts: rmpp-korean 0.1.0's Noto Sans CJK KR files in `/home/root/.local/share/fonts/rmpp-korean/` are package
  files and go with the upgrade; rmpp-lang-ko ships the same files in
  `/home/root/.local/share/rmpp-lang/languages/ko/fonts/`. With `font.ko` empty, Hangul looks exactly as before.
  Fonts a user had dropped into the old folder are not package files: rmpp-lang-ko's postinstall can move them to
  `/home/root/.local/share/fonts/rmpp-lang/ko/`:

```sh
[ -d /home/root/.local/share/fonts/rmpp-korean ] && mkdir -p /home/root/.local/share/fonts/rmpp-lang/ko \
  && for f in /home/root/.local/share/fonts/rmpp-korean/*.[ot]t[fc]; do
       [ -e "$f" ] && mv -n "$f" /home/root/.local/share/fonts/rmpp-lang/ko/; done
rmdir /home/root/.local/share/fonts/rmpp-korean 2>/dev/null || true
```

## Transitional rmpp-korean 0.2.0

```sh
pkgver=0.2.0; pkgrel=0
depends="rmpp-lang rmpp-lang-ko"
package() { mkdir -p "$pkgdir"; }      # no files
postupgrade() { :; }                   # rmpp-lang's postinstall already removed the old link and /usr/lib/rmpp-korean
```

apk upgrades rmpp-korean 0.1.0 -> 0.2.0, which pulls rmpp-lang and rmpp-lang-ko; 0.1.0's files in
`/home/root/.vellum/share` and its fonts are removed as package files, rmpp-lang's postinstall cleans `/usr/lib`.
Check on the tablet that the order (rmpp-lang postinstall before or after rmpp-korean's upgrade) leaves exactly one
preload link and Korean on.

## Development installs

`deploy.sh` (this repository) installs rmpp-lang and the add-ons in `ADDONS` (default `ko`) next to an installed
rmpp-korean package by moving its preload link to `/usr/lib/rmpp-lang/50-rmpp-korean.so.off`;
`deploy.sh HOST --remove` puts it back.
