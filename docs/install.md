# Installing rmpp-lang

The usual way is **rmpp setup** from [rmpp-packages](https://github.com/lorancecall/rmpp-packages#install), as the
desktop app or the script: tick **Languages: keyboards and fonts per language**, then the languages you want. This
page covers the rest. Steps that are the same for every rmpp add-on live in rmpp-packages and are linked here, not
repeated.

## Packages

rmpp-lang is a core plus one package per language.

| Package | Installs with it |
|---|---|
| `rmpp-lang` | `rmpp-preload` |
| `rmpp-lang-ko` Korean | `rmpp-lang`, `rmpp-preload` |
| `rmpp-lang-ja` Japanese | `rmpp-lang`, `rmpp-lang-ko` (typing Japanese by sound in Hangul), `rmpp-preload` |
| `rmpp-lang-zh` Chinese | `rmpp-lang`, `rmpp-lang-ko` (typing Chinese in Hangul or Hanja readings), `rmpp-preload` |
| `rmpp-lang-ru` Russian | `rmpp-lang`, `rmpp-preload` |
| `rmpp-lang-bg` Bulgarian | `rmpp-lang`, `rmpp-preload` |

Dependencies are installed automatically, in rmpp setup and with `vellum add`. All packages and what needs what:
[packages.md](https://github.com/lorancecall/rmpp-packages/blob/main/docs/packages.md).

## Without rmpp setup

Follow [manual-install.md](https://github.com/lorancecall/rmpp-packages/blob/main/docs/manual-install.md) (Vellum
and the rmpp package repository), then on the tablet, one `rmpp-lang-<code>` per language:

```sh
vellum add rmpp-lang-ko
vellum add rmpp-lang-ja rmpp-lang-zh rmpp-lang-ru rmpp-lang-bg    # any of these
```

## Uninstall

- **One language:** untick it in rmpp setup and press **Remove unticked installed**, or
  `vellum del rmpp-lang-<code>`. Japanese and Chinese need `rmpp-lang-ko`: remove them before Korean.
- **All of rmpp-lang:** untick **Languages** in rmpp setup and press **Remove unticked installed**, or remove the
  language packages and the core together: `vellum del rmpp-lang-ko rmpp-lang` (add the other `rmpp-lang-<code>`
  you have).
- **What stays:** your settings in `/home/root/.config/rmpp-lang` (languages, order, fonts chosen), the folder
  `/home/root/.local/share/rmpp-lang`, and fonts you added (`/home/root/.local/share/fonts/rmpp-lang`). Delete
  them by hand to start fresh, or
  use step 3 of [uninstall.md](https://github.com/lorancecall/rmpp-packages/blob/main/docs/uninstall.md).
- **Everything rmpp, and before a factory reset:**
  [uninstall.md](https://github.com/lorancecall/rmpp-packages/blob/main/docs/uninstall.md).

## Next to other mods

`rmpp-lang` cannot be installed next to `rmpp-korean` (its predecessor; they do the same): remove that one first
(`vellum del rmpp-korean`; see [MIGRATION.md](MIGRATION.md)).

It runs next to xovi and other Vellum packages:
[how-it-works.md](https://github.com/lorancecall/rmpp-packages/blob/main/docs/how-it-works.md#next-to-other-mods).

## Build and install by hand

For developers. Requires an aarch64 cross toolchain (Arm GNU `aarch64-none-linux-gnu`, GCC 14), Qt 6.10 headers
(QtCore, QtGui, QtQml, QtQuick), the device's Qt libraries to link against, and `pyside6-rcc`. The tablet needs
rmpp-preload with the `/usr/lib/rmpp` layout (installed with any rmpp add-on).

```sh
SDK=/path/to/sdk sh build.sh           # build/librmpp-lang.so, build/rmpp-lang.rcc, build/rmpp-lang
sh tools/build-addon.sh ko ru          # build/languages/<code>/ and build/languages/<code>.rcc
ADDONS="ko ru" sh deploy.sh rmpp       # development install over ssh (host alias "rmpp")
sh deploy.sh rmpp --remove             # remove it again
```

Where the files go on the tablet:

| Path | What |
|---|---|
| `/usr/lib/rmpp/rmpp-lang/` | `librmpp-lang.so`, `rmpp-lang.rcc`, the `rmpp-lang` command, `languages/<code>.rcc` (on-screen layouts) |
| `/usr/lib/rmpp/preload.d/50-rmpp-lang.so` | link to the library; rmpp-preload loads it into xochitl at boot |
| `/home/root/.local/share/rmpp-lang/languages/<code>/` | each language add-on: manifest, key maps, dictionaries, fonts, `glyphs.rmgl` |
| `/home/root/.config/rmpp-lang/` | settings, `state.json`, `fonts.json` |

The root file system is read-only and an OS update replaces it; `deploy.sh` remounts it read-write only while it
copies. Never run `systemctl stop xochitl` on the device: stopping it powers the tablet off. Run
`systemctl reset-failed xochitl` before a restart (`deploy.sh` does): more than four xochitl restarts in ten
minutes reboots the tablet.

Each add-on also carries `glyphs.rmgl`, pen strokes of its font for text written as ink (rmpp-textobj, rmpp-hwr);
every non-ASCII glyph lives in these files. They are committed; rebuild one after changing its `glyph_font` /
`glyph_chars` (needs Python with numpy, Pillow, fontTools): `python3 tools/glyphs/glyphs.py build zh` (see
"Glyph files" in [DESIGN.md](DESIGN.md)).
