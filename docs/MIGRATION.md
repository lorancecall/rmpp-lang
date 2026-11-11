# Migration: rmpp-korean -> rmpp-lang

Proposal, to agree with whoever maintains the package repository and installer before anything public changes.
Nothing here is done by the `feature/rmpp-lang` branch itself.

## Order

1. **Merge this branch** (after review) into `main` of `lorancecall/rmpp-korean`.
2. **Rename the repository** `lorancecall/rmpp-korean` -> `lorancecall/rmpp-lang`. GitHub redirects the old URLs
   (web, git, release downloads), so the rmpp-korean 0.1.0 package keeps building and existing links keep working.
   Update the README title and links in the same step (README is left as rmpp-korean on this branch on purpose).
3. **Release v0.1.0 of rmpp-lang** with the assets `librmpp-lang.so`, `rmpp-lang.rcc`, `rmpp-lang` (tablet
   command) and `LICENSE`. Fonts are not release assets: the package fetches Noto Sans CJK KR from notofonts at a
   pinned commit as rmpp-korean does.
4. **Packages** (rmpp-packages):
   - New `packages/rmpp-lang/VELBUILD` (0.1.0-r0), `replaces="rmpp-korean"` so it may take over files.
   - `packages/rmpp-korean` 0.2.0-r0 becomes an empty transitional package: `depends="rmpp-lang"`, no files, its
     postinstall only removes what 0.1.0 left in `/usr/lib` (see below). `vellum upgrade` then moves every
     rmpp-korean user to rmpp-lang without a manual step; it can be dropped from the index later.
5. **Installer**: `mods.txt` entry `rmpp-korean` -> `rmpp-lang` (keep `default=1`), new `rmpp-mod.conf` texts
   (below). When the installer finds rmpp-korean installed it can simply `vellum add rmpp-lang` (the
   transitional package covers the rest).
6. **rmpp-hwr v0.3** reads `/home/root/.config/rmpp-lang/state.json` (format in DESIGN.md) for languages and fonts,
   and falls back to its own defaults when the file is missing.

## rmpp-lang package

Files (as in rmpp-korean, new names):

| Package file | Copied to (postinstall / postupgrade / reenable) |
|---|---|
| `/home/root/.vellum/share/rmpp-lang/librmpp-lang.so` | `/usr/lib/rmpp-lang/librmpp-lang.so` |
| `/home/root/.vellum/share/rmpp-lang/rmpp-lang.rcc` | `/usr/lib/rmpp-lang/rmpp-lang.rcc` |
| `/home/root/.vellum/share/rmpp-lang/rmpp-lang` | `/usr/bin/rmpp-lang` (755) |
| `/home/root/.local/share/fonts/rmpp-lang/ko/NotoSansCJKkr-{Regular,Bold}.otf` | (stays in /home) |

postinstall / postupgrade (root remounted rw as now):

```sh
ln -sf /usr/lib/rmpp-lang/librmpp-lang.so /usr/lib/rmpp-preload.d/50-rmpp-lang.so
# rmpp-korean 0.1.0: both libraries would compose Hangul twice
rm -f /usr/lib/rmpp-preload.d/50-rmpp-korean.so
rm -rf /usr/lib/rmpp-korean
# Keep settings: rmpp-korean had no settings file; start rmpp-lang with the same behaviour
[ -f /home/root/.config/rmpp-lang/lang.conf ] || { mkdir -p /home/root/.config/rmpp-lang
  printf 'languages=ko,en\nkeyboard.ko=1\n' > /home/root/.config/rmpp-lang/lang.conf; }
# Fonts a user dropped next to rmpp-korean's (package files are removed by apk itself)
[ -d /home/root/.local/share/fonts/rmpp-korean ] && for f in /home/root/.local/share/fonts/rmpp-korean/*.[ot]t[fc]; do
  [ -e "$f" ] && mv -n "$f" /home/root/.local/share/fonts/rmpp-lang/ko/; done
rmdir /home/root/.local/share/fonts/rmpp-korean 2>/dev/null || true
```

then `reset-failed` + `restart xochitl` as now. preremove: remove the link, `/usr/lib/rmpp-lang`, `/usr/bin/rmpp-lang`;
keep `/home/root/.config/rmpp-lang` and user fonts (a reinstall finds them again).

What "keeping settings" covers:

- Which on-screen languages are on (Korean in Language & keyboard) is xochitl's setting: untouched. The layout id
  stays `ko_KR`, so Korean stays selected.
- rmpp-bt's per-keyboard Type Folio mode (`/home/root/.config/rmpp-bt/keyboards.conf`) is read the same way.
- The Korean font: with `font.ko` empty, Hangul falls back exactly as before (fontconfig finds Noto Sans CJK KR in
  the new folder).

## Transitional rmpp-korean 0.2.0

```sh
pkgver=0.2.0; pkgrel=0
depends="rmpp-lang"
package() { mkdir -p "$pkgdir"; }      # no files
postupgrade() { :; }                   # rmpp-lang's postinstall already removed the old link and /usr/lib/rmpp-korean
```

apk upgrades rmpp-korean 0.1.0 -> 0.2.0, which pulls rmpp-lang; 0.1.0's files in `/home/root/.vellum/share` and the
fonts are removed as package files, rmpp-lang's postinstall cleans `/usr/lib`. Check on the tablet that the order
(rmpp-lang postinstall before or after rmpp-korean's upgrade) leaves exactly one preload link.

## rmpp-mod.conf (proposed)

See `rmpp-mod.conf` on this branch (id `rmpp-lang`, `addons=` list of language add-ons published by rmpp-packages;
the installer shows them indented under rmpp-lang, Korean always included).

## Development installs

`deploy.sh` (this repository) installs rmpp-lang next to an installed rmpp-korean package by moving its preload
link to `/usr/lib/rmpp-lang/50-rmpp-korean.so.off`; `deploy.sh HOST --remove` puts it back.
