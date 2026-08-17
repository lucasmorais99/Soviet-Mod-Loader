# Maintainer guide

This document covers maintenance, upstream synchronization, validation, and
release of Soviet Mod Loader.

## Distribution model

TesmioLoader is the host: it discovers DLLs and provides configuration, logging,
services, and the VFS. SML is one opinionated plugin containing four content
capabilities:

| Capability | Embedded source | Responsibility |
|---|---|---|
| buildings | `vendor/tesmio/buildings.cpp` | Materialize generated Workshop buildings. |
| resources | `vendor/tesmio/resources.cpp` | Extend the resource registry. |
| deposits | `vendor/tesmio/deposits.cpp` | Add types, DDS channels, generation, and persistence. |
| needs | `vendor/tesmio/needs.cpp` | Extend citizen needs and dependent hooks. |

The sources compile as separate translation units with renamed, non-exported
entry points. The final DLL exports only SML. Independent file-scope state is
preserved while the manager controls every `Init` and `Start` call.

The upstream standalone buildings build is parked in b0.3.6, but remains
bundled because it is address-independent and required by SML's integrated
catalog and `workshop_wip` safety checks.

## Compatibility baseline

- TesmioLoader b0.3.6;
- host API 4; child hooks may accept API 3 through 4;
- game v1.1.1.9;
- upstream baseline `3baa141f9f08921aea9c95f0a400289cabd9960a`;
- plugin filename `soviet_mod_loader.dll`, without `000_`.

API 4 adds `TsmHost::vfsRoot`. SML checks `structSize` before reading it and
retains its older path resolver as a defensive fallback. Every embedded binary
hook keeps upstream version and prologue verification.

Review the upstream
[changelog](https://github.com/MaxLegend/TesmioLoader/blob/master/changelog.md),
[architecture](https://github.com/MaxLegend/TesmioLoader/blob/master/docs/01-architecture.md),
and [API header](https://github.com/MaxLegend/TesmioLoader/blob/master/src/tesmio_api.h)
before synchronization.

## Startup flow

1. Discover Workshop manifests and validate API ranges.
2. Resolve dependencies and deterministic order.
3. Plan catalogs, conflicts, INI merges, and assets entirely in memory.
4. Enforce operational invariants and validate cross-domain references.
5. Block unsafe reserved folders under `media_soviet/workshop_wip`.
6. Display confirmation when the configured policy requires it.
7. If selected, back up `media_soviet/save` before any SML mutation.
8. Apply accepted snapshots, catalogs, INIs, assets, and plugin disabling.
9. Initialize resources, deposits, needs, and buildings in dependency order.
10. Compare planned and runtime catalogs, patches, and building results.
11. Initialize child hooks and complete dependent hooks during `Start`.
12. Persist `confirmation.index` only after all validation succeeds.

Declining or closing the dialog exits before application. Backup failure also
exits before application. `confirmation_mode = never` is the explicit
non-interactive path and does not create a launch backup.

## Save backup contract

The native Task Dialog exposes **Back up my saved games before loading mods**
with `TDF_VERIFICATION_FLAG_CHECKED`. Its returned verification state is read
only after the user accepts. The MessageBox fallback cannot expose a checkbox,
so it uses the checked default.

The complete source tree is:

```text
<game>/media_soviet/save
```

Each backup uses a fresh destination:

```text
<game>/media_soviet/save_backups/SML-YYYYMMDD-HHMMSS[-N]
```

The destination is outside the source tree. An absent save directory is a
successful no-op. Directory creation or copy errors are logged, displayed in
English, and terminate with `ERROR_WRITE_FAULT`; an incomplete destination is
left available for diagnosis and is never deleted automatically.

## Required component invariants

Content requirements override an old baseline or `allow_settings`:

- non-empty resources: `hook = 2`;
- non-empty deposits: `code_patch = 1`;
- non-empty needs: `enabled = demand = storage = 1`;
- non-empty buildings: `enabled = 1` and
  `out = media_soviet\workshop_wip`.

Diagnostics, prices, balancing, and UI preferences remain configurable.
Private link-time status functions report counts and hook state; they do not
change `SmlApi`. A planned/runtime mismatch, rejected mandatory patch, or
`INCOMPLETE` building blocks startup. Needs completes validation during `Start`.

The universal deposit generator accepts per-deposit `richness_offset` from
`-0.25` through `+0.25`. It is applied before the fixed `0.61` threshold and
therefore changes coverage and intensity. Zero retains the 0.5.1 distribution.
Initialized manifest entries never regenerate after a balance change.

## Upstream update procedure

1. Record the new SHA and read the complete upstream changelog.
2. Diff `tesmio_api.h` and `tesmio_plugin.h`; synchronize headers first.
3. Compare resources, needs, deposits, and buildings independently.
4. Reapply only intentional SML adaptations: local include, renamed entry
   points, centralized initialization, universal deposits, and save backup.
5. Revalidate every RVA, prologue, and game-version gate.
6. Update `NOTICE.md`, README, guides, SML version, and baseline SHA.
7. Run automated checks and manual game tests in a disposable profile.

Keep resources and needs close to active upstream sources. Deposits includes
the SML generation/persistence layer. Buildings requires manual review because
its active upstream build is parked.

## Build and tests

Requirements are Windows and Visual Studio Build Tools with the x64 C++
toolset. From the repository root:

```bat
test.bat
build.bat
```

Distributable output:

```text
build/plugins/soviet_mod_loader.dll
build/plugins/soviet_mod_loader.ini
```

The project uses C++17 and static runtime `/MT`. The build script removes old
`000_` artifacts.

Minimum release checks:

- `test.bat` and `build.bat` pass;
- the DLL exposes exactly the three Tesmio exports;
- confirmation accept, decline, checked backup, and unchecked backup work;
- backup copy failure blocks before application;
- `workshop_wip` mismatch blocks safely;
- clean install works without standalone component INIs;
- resource hook mode is normalized from 1 to 2 when required;
- missing references and incomplete buildings block at their expected phases;
- new deposit, save, reload, late-installed deposit, and `richness_offset` work;
- `saved_last`, `campaign1`, `save/<world>`, traversal, and external absolute
  paths follow the manifest path policy;
- README, guides, version, and upstream SHA are current.

## Release and migration

Distribute the DLL, INI, documentation, and template. Do not distribute the
four standalone components. Release notes must tell old users to remove:

```text
tesmioloader/build/plugins/000_soviet_mod_loader.dll
tesmioloader/build/plugins/000_soviet_mod_loader.ini
```

The old INI is accepted only as a migration fallback. Two DLL copies are
unsafe.

Persistent manager state belongs under
`tesmioloader/build/soviet_mod_loader` or the configured `state_dir`. Never
package or casually delete it: `catalog.ini` preserves stable assignments.
Saved-game backups are user data under `media_soviet/save_backups` and must
never be removed by build, update, or cleanup scripts.

## Diagnostics

Start with `tesmioloader.log` and `soviet_mod_loader/report.json`. Confirm:

- exact game and loader versions;
- no standalone components or legacy SML DLL remain;
- reserved WIP folders match the catalog;
- `catalog.ini` and per-world manifests are preserved;
- the backup destination and any Windows copy error;
- separate milestones for DDS generation, texture loading, resource
  registration, and building validation.

Never resolve an incident by automatically deleting saves, backups, or WIP
folders. SML fails closed and leaves destructive choices to the user.
