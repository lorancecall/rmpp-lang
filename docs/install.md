# Installing rmpp-lang without rmpp setup

The usual way is **rmpp setup** from [rmpp-packages](https://github.com/lorancecall/rmpp-packages#install): see
[Install](../README.md#install). This page covers the other ways and removal.

## With Vellum on the command line

On the tablet (over SSH), with [Vellum](https://github.com/vellum-dev/vellum) already installed, add the
rmpp-packages repository and its signing key:

```sh
wget -qO- https://lorancecall.github.io/rmpp-packages/install.sh | sh
```

Then install the core and one package per language:

```sh
vellum add rmpp-lang rmpp-lang-ko          # Korean
vellum add rmpp-lang-ja rmpp-lang-zh       # Japanese, Chinese (they pull in rmpp-lang-ko for their Hangul input)
vellum add rmpp-lang-ru rmpp-lang-bg       # Russian, Bulgarian
```

- Updates: `vellum upgrade`.
- After a reMarkable OS update: `vellum reenable`. The update replaces the root file system, which holds the core.
- `rmpp-lang` replaces the old `rmpp-korean`: remove that one first (`vellum del rmpp-korean`); see
  [MIGRATION.md](MIGRATION.md).

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

## Removal

- One language: `vellum del rmpp-lang-<code>`. All of rmpp-lang: the language packages and the core together,
  for example `vellum del rmpp-lang-ko rmpp-lang`.
- A development install: `sh deploy.sh rmpp --remove`.
- All rmpp add-ons: **Remove everything** in rmpp setup (do this before a factory reset or handing the tablet on).
