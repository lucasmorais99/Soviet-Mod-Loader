# Soviet Mod Loader

Soviet Mod Loader (SML) 0.6.0 is a single TesmioLoader plugin that discovers,
orders, and merges compatible mods directly from the Steam Workshop. The DLL
includes the `buildings`, `resources`, `deposits`, and `needs` capabilities, so
users must not install those plugins separately.

This release targets TesmioLoader b0.3.6/API 4 and *Workers & Resources: Soviet
Republic* v1.1.1.9. TesmioLoader provides the plugin host and VFS; SML provides
the centralized mod-content workflow.

## Documentation

- [Maintainer guide](docs/GUIA_MANTENEDOR.md): architecture, upstream sync,
  builds, tests, diagnostics, and releases.
- [User guide](docs/GUIA_USUARIO.md): installation, mod confirmation, save
  backups, updates, and troubleshooting.
- [Mod developer guide](docs/GUIA_MODDERS.md): manifests, INI fragments,
  assets, dependencies, deposit generation, and native hooks.
- [Architecture reference](docs/ARCHITECTURE.md): internal data flow and
  compatibility boundaries.

A complete starter mod is available under [`template/simple-mod`](template/simple-mod).

## Quick installation

1. Close the game and remove old `000_soviet_mod_loader.dll` and
   `000_soviet_mod_loader.ini` files.
2. Copy `soviet_mod_loader.dll` and `soviet_mod_loader.ini` to
   `tesmioloader/build/plugins`.
3. Remove or disable standalone `buildings.dll`, `resources.dll`,
   `deposits.dll`, and `needs.dll` copies.
4. Launch the game through `tesmiolauncher.exe`.

The historical `000_` prefix is no longer used. Keeping both DLL names in the
same installation can load duplicate logic.

SML does not depend on the configuration files formerly shipped with the four
standalone plugins. When content requires a capability, SML enforces its safe
operating mode internally. It validates cross-catalog references before
applying files and validates the actual component registrations and hooks
before allowing the game to continue.

When the mod configuration changes, the confirmation dialog offers to back up
saved games before loading the new set. This option is selected by default.
Backups are written below `media_soviet/save_backups`.

## Upstream sources

The integration contract comes from
[TesmioLoader](https://github.com/MaxLegend/TesmioLoader), particularly its
[public API](https://github.com/MaxLegend/TesmioLoader/blob/master/src/tesmio_api.h),
[plugin documentation](https://github.com/MaxLegend/TesmioLoader/blob/master/docs/09-plugins.md),
and [changelog](https://github.com/MaxLegend/TesmioLoader/blob/master/changelog.md).
Intentional adaptations are documented in the maintainer guide and `NOTICE.md`.
