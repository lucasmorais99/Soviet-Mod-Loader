# DISCLAIMER: Artificial Intelligence (AI) has been used throughout developing this project. It has enabled me, a long-time fan of modding games with an average technical expertise, to share my passion with many others around the world.

# Soviet Mod Loader

Workshop-first mod management implemented as one TesmioLoader API v3 plugin for
*Workers & Resources: Soviet Republic*. The upstream `buildings`, `resources`,
`deposits`, and `needs` implementations are compiled into the same DLL, so
their configuration is consumed only after the Workshop merge completes.

## Install

1. Run `build.bat` from a Visual Studio C++ developer environment (or let the
   script locate Build Tools).
2. Copy `build/plugins/000_soviet_mod_loader.dll` and its `.ini` into the
   existing `tesmioloader/plugins` directory.
3. Remove or disable the separate `buildings.dll`, `resources.dll`,
   `deposits.dll`, and `needs.dll`. The Soviet Mod Loader also writes their
   `[plugins]` keys to `0` automatically; if one was already loaded on the first
   launch, restart once. Launch through `tesmiolauncher.exe` as usual.

After this one-time plugin installation, subscribing, updating, disabling in a
manifest, or removing a compatible Workshop mod needs no post-install action.
The current result is written to `soviet_mod_loader/report.json` and the normal
`tesmioloader.log`.

Before applying a changed mod set, the loader shows a native Windows
confirmation with the load order and warnings. Refusing closes the game before
the SML writes merged INIs, synchronizes assets, or installs hooks. Configure
`confirmation_mode` in `000_soviet_mod_loader.ini` as `changes` (default),
`always`, or `never`; `never` is intended for unattended launches and UI
recovery.

As a crash-prevention check, SML also compares its planned building catalog
with numeric folders in `media_soviet/workshop_wip` from `9100000000` through
`9199999999`. Unexpected folders, missing SML stamps, or stamps belonging to a
different building stop launch with an explicit list of folders to delete.
SML never deletes these folders automatically.

## Create a mod

Copy `template/simple-mod` into a Workshop content folder or your Workshop WIP
folder. Edit `soviet.mod.ini`, keep a globally unique reverse-DNS id, and put
only the INI fragments you need under `tesmio/`. Asset paths below `assets/`
must match their final paths below TesmioLoader's `vfs/` directory.

Dependencies use `mod.id = constraint`, for example `>=1.2.0`. Optional native
hooks must themselves be TesmioLoader plugins and are listed as repeatable
`dll = hooks\\name.dll` entries. Put service consumption and game hooks in
`TsmPluginStart`, not `TsmPluginInit`.

Use ASCII for Tesmio INI tokens and save files as UTF-8 without BOM. Explicit
resource slot numbers are removed during merge. Do not declare `id` in a
building or `type`, `map`, or `component` in a deposit. Every consolidated
deposit receives `map = auto`, without `component`, and the incorporated
TesmioLoader deposits implementation performs its internal channel allocation.
Test save compatibility on a copy because adding resources/needs changes data
the game serializes.

The persistent catalog lives at `tesmioloader/build/soviet_mod_loader/catalog.ini`
in a source layout (beside the other loader state). Never delete it from an
active installation: it keeps building IDs, deposit types, and list
ordering constant as subscriptions are added or removed.

Assets are synchronized into `tesmioloader/vfs`. When the DLL is running from
`tesmioloader/build`, the loader deliberately uses the sibling `../vfs` folder;
it does not create `build/vfs`.

See [architecture](docs/ARCHITECTURE.md) and the public
[`SmlApi`](include/soviet_mod_loader_api.h) iteration interface.

## Restore local base configuration

The first launch preserves the original four Tesmio INIs under
`soviet_mod_loader/base`. Edit those snapshots for machine-local defaults; the
next launch regenerates the merged files. Deleting a snapshot imports the
current non-generated target once, if present.

## Build inside TesmioLoader

This repository uses the same `/MT` and native DLL patterns as upstream. The
four vendored implementations are separate translation units linked into the
manager DLL, avoiding collisions between their file-scope state. Build with
`/std:c++17`; no third-party library is required.
