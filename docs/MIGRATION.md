# From rmpp-korean to rmpp-lang

There is no migration path: rmpp-korean 0.1.0 had a single user (the developer), so it is removed and rmpp-lang is
installed fresh. No transitional package, no `replaces=`, no settings carried over.

## Release order (each public step needs the developer's OK)

1. Merge `feature/rmpp-lang` into `main` of `lorancecall/rmpp-lang` (PR #1). The old repository
   `lorancecall/rmpp-korean` was deleted by the developer; `lorancecall/rmpp-lang` was created fresh from the local
   history (its `main` is rmpp-korean 0.1.0), so no rename step.
2. Tag v0.1.0 with the core assets `librmpp-lang.so`, `rmpp-lang.rcc`, `rmpp-lang` (tablet command), `LICENSE`.
   Add-ons are built from `languages/<code>/` of the same tag (`tools/build-addon.sh`).
3. rmpp-packages: core package `rmpp-lang`, generated `rmpp-lang-<code>` packages (Korean `rmpp-lang-ko`,
   `default=1`), installer entry from `rmpp-mod.conf`. Remove the `rmpp-korean` package from the repository and
   `mods.txt`.
4. rmpp-hwr v0.3 reads `/home/root/.config/rmpp-lang/state.json` and `fonts.json`.

## On the developer's tablet

```sh
vellum del rmpp-korean                  # removes its library, layout, preload link and fonts
vellum add rmpp-lang rmpp-lang-ko       # (or the rmpp installer: rmpp-lang with Korean selected)
```

Untouched by the switch: which on-screen languages are on (xochitl's setting; the layout id stays `ko_KR`, so
Korean comes back when rmpp-lang-ko registers it) and rmpp-bt's per-keyboard Type Folio mode.

## rmpp-lang (core) package

Up to 0.3.1 (below, kept for history). Since 0.3.2 the files are copied to one folder, `/usr/lib/rmpp/rmpp-lang/`
(see "Pieces" in [DESIGN.md](DESIGN.md)), linked from `/usr/lib/rmpp/preload.d/`; the 0.3.2 package removes
`/usr/lib/rmpp-lang`, `/usr/bin/rmpp-lang` and the old preload link.

| Package file | Copied to (postinstall / postupgrade / reenable) |
|---|---|
| `/home/root/.vellum/share/rmpp-lang/librmpp-lang.so` | `/usr/lib/rmpp-lang/librmpp-lang.so` |
| `/home/root/.vellum/share/rmpp-lang/rmpp-lang.rcc` | `/usr/lib/rmpp-lang/rmpp-lang.rcc` |
| `/home/root/.vellum/share/rmpp-lang/rmpp-lang` | `/usr/bin/rmpp-lang` (755) |

postinstall / postupgrade: `mkdir -p /usr/lib/rmpp-lang/languages`, link
`/usr/lib/rmpp-preload.d/50-rmpp-lang.so`, then `reset-failed` + restart xochitl. Without a `lang.conf` the core
starts with English only; each add-on's postinstall turns its language on (`rmpp-lang lang on <code>`).
preremove: remove the link, `/usr/lib/rmpp-lang`, `/usr/bin/rmpp-lang`; keep `/home/root/.config/rmpp-lang` and
user fonts.

## Development installs

`deploy.sh` installs rmpp-lang and the add-ons in `ADDONS` (default `ko`) the way the package does, in
`/usr/lib/rmpp/rmpp-lang/` (needs rmpp-preload with the `/usr/lib/rmpp` layout). Up to 0.3.1 it also parked rmpp-korean's preload link; rmpp-korean is gone now.
