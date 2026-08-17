# User guide

Soviet Mod Loader (SML) loads compatible mods directly from the Steam Workshop.
One DLL provides building, resource, deposit, and need support; the four
standalone plugins are not required.

## Requirements

- *Workers & Resources: Soviet Republic* v1.1.1.9;
- TesmioLoader b0.3.6;
- Windows.

Launch the game with `tesmiolauncher.exe`, not directly from Steam. Keep a
separate copy of important saves before changing a mod set.

## Installation

1. Close the game.
2. Open `tesmioloader/build/plugins`.
3. Delete old `000_soviet_mod_loader.dll` and `000_soviet_mod_loader.ini`
   files if present.
4. Remove or disable standalone `buildings.dll`, `resources.dll`,
   `deposits.dll`, and `needs.dll` copies.
5. Copy `soviet_mod_loader.dll` and `soviet_mod_loader.ini` into the folder.
6. Open `tesmiolauncher.exe`, enable SML if a plugin list is displayed, and
   start the game.

Do not rename the DLL to restore the `000_` prefix. TesmioLoader separates
`Init` and `Start`, and SML controls the order of its embedded components.

## Normal workflow

Subscribe to a compatible Workshop item and launch through TesmioLoader. SML:

1. discovers installed mods;
2. validates versions and dependencies;
3. establishes a stable load order;
4. plans conflicts, catalogs, INIs, and assets in memory;
5. validates resource references and building donors;
6. asks for confirmation when required;
7. applies the accepted plan and validates its runtime components.

The first run, or a change to the list, order, version, fingerprint, or state,
opens a native confirmation dialog. Choose **Load mods and start** to continue.
Declining, closing the dialog, or pressing Escape exits without applying the
new plan.

### Save backup option

The confirmation dialog includes **Back up my saved games before loading
mods**, selected by default. When accepted, SML copies the complete folder:

```text
SovietRepublic/media_soviet/save
```

to a timestamped directory such as:

```text
SovietRepublic/media_soviet/save_backups/SML-20260816-143000
```

The backup runs before SML changes INIs, catalogs, assets, plugin settings, or
hooks. If it fails, SML displays an English error and closes the game without
applying the plan. When the Task Dialog is unavailable, the MessageBox fallback
uses the safe default and creates the backup after the user selects Yes.

No backup is created when the confirmation is skipped because nothing changed,
or when `confirmation_mode = never` is configured.

## Confirmation policy

`confirmation_mode` in `soviet_mod_loader.ini` accepts:

- `changes` (default): ask only when the resolved mod configuration changes;
- `always`: ask on every launch;
- `never`: never show a dialog, intended for unattended runs or UI recovery.

## Mod states

| State | Meaning |
|---|---|
| active / added | The mod will load. |
| conflict | The mod loads, but later content replaced one of its entries. |
| disabled | The manifest disabled the mod. |
| incompatible | The mod does not accept the current TesmioLoader API. |
| missing dependency | A required mod or compatible version is absent. |
| error | The manifest or content is invalid. |

Dependencies load first. Priority, Workshop addition time, and mod ID then
provide deterministic ordering. Later entries win individual conflicts.

## `workshop_wip` safety warning

Before confirmation, SML compares planned building IDs with numeric folders in
`media_soviet/workshop_wip` from `9100000000` through `9199999999`. An unknown
folder, missing stamp, or mismatched stamp can crash the game.

SML lists unsafe folders and blocks startup. Delete only the folders named in
the message, then launch again. SML never deletes them automatically.

## Updates and removal

To update, close the game, replace the DLL, optionally merge new INI comments,
and relaunch. Preserve `tesmioloader/build/soviet_mod_loader`: its append-only
catalog keeps save-facing building IDs, deposit types, and list positions
stable. Managed assets belong in the real `tesmioloader/vfs`, never
`tesmioloader/build/vfs`.

To remove SML, close the game and remove its DLL and INI. Saves containing SML
resources, needs, deposits, or buildings may still require the same mod set;
restore a backup if they cannot load safely.

## Troubleshooting

**The confirmation appears every time:** verify `confirmation_mode = changes`
and check whether Workshop files are being modified continuously.

**The game closes after declining:** this is expected; the new plan was not
applied.

**The backup fails:** check free space and permissions for
`media_soviet/save_backups`. The error and destination are also written to
`tesmioloader.log`.

**Startup lists WIP folders:** delete only the listed folders and retry.

**A resource is not registered:** a building, deposit, or need references an
absent resource. Update or remove the offending mod; do not work around the
block by lowering the resources hook mode.

**A donor, `.nmf`, or `.mtl` is missing:** the generated building would be
incomplete. Its author must use a complete donor compatible with this game
version.

**The log says a DDS was generated but startup is blocked:** DDS generation,
texture loading, resource registration, and building validation are separate
stages. A generated map cannot make a missing resource pointer safe.

**Deposit richness changes only in new worlds:** mod authors may declare
`richness_offset`, but it applies only when SML creates a new channel. Existing
channels are preserved to protect painting and depletion data.

**Version or prologue error:** the game was probably updated. Install a matching
SML release instead of forcing the hook.

For support, provide `tesmioloader.log`,
`tesmioloader/build/soviet_mod_loader/report.json`, and the exact game,
TesmioLoader, and SML versions. Review personal saves before sharing them.
