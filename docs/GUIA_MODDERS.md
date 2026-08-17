# Mod developer guide

SML reads mods directly from their Steam Workshop directories. A mod consists
of a `soviet.mod.ini` manifest, optional content fragments, VFS-ready assets,
and optional native TesmioLoader hooks.

Start by copying [`template/simple-mod`](../template/simple-mod), then remove
the domains your mod does not use.

## Recommended layout

```text
my-mod/
|-- soviet.mod.ini
|-- tesmio/
|   |-- buildings.ini
|   |-- resources.ini
|   |-- deposits.ini
|   `-- needs.ini
|-- assets/
|   `-- media_soviet/...
`-- hooks/
    `-- my_hook.dll
```

Paths below `assets` must match their final paths inside `tesmioloader/vfs`.
Do not publish generated catalogs, merged INIs, or per-world DDS files.

## Manifest

```ini
[mod]
id = org.example.simple.industry
name = Simple Industry
version = 1.0.0
enabled = 1
priority = 0
tesmio_api_min = 3
tesmio_api_max = 4

[dependencies]
org.example.library = >=1.2.0

[content]
resources = tesmio\resources.ini
deposits = tesmio\deposits.ini
needs = tesmio\needs.ini
buildings = tesmio\buildings.ini
assets = assets

[hooks]
dll = hooks\my_hook.dll
```

- `id` is a permanent globally unique identifier, preferably reverse DNS.
- `version` follows semantic versioning.
- `enabled = 0` disables the complete item.
- Higher `priority` values apply later.
- `tesmio_api_min/max` define the accepted host API range.
- Dependencies use `mod.id = version constraint`.
- `dll` may be repeated for multiple hooks.

Dependencies precede dependants. Priority, addition time, and mod ID then
produce a stable order. When two mods define the same entry, the later entry
wins; unrelated content from the earlier mod remains active.

## Internal numeric catalog

Authors declare stable names, not globally coordinated numbers:

- do not declare `id` in a `buildings.ini` section;
- do not declare `type`, `map`, or `component` in `deposits.ini`;
- do not prefix resource or need entries with explicit slots;
- keep section names stable and unique within your mod.

SML assigns append-only building IDs, deposit types, and list positions. Mod
deposits are emitted as `map = auto`; the embedded component assigns a unique
map/channel. The portable identity is the mod ID plus the section name, not a
number observed on one installation.

## Content fragments

Include only your own content sections. Global component settings are protected
unless `[content] allow_settings = 1` is explicitly enabled. Required safety
settings remain SML invariants even then.

Conflicts in `resources` and `needs` are resolved per section/key. A named
`buildings` or `deposits` section is atomic so repeatable keys such as `line`
remain together.

SML validates `$PRODUCTION`, `$CONSUMPTION`,
`$CONSUMPTION_PER_SECOND`, storage resources, resource clones, deposit icons,
and needs before confirmation. Missing references block the complete plan
before any file is applied.

A generated building donor must provide at least:

```text
media_soviet/buildings_types/<donor>.ini
media_soviet/buildings/<donor>.nmf
media_soviet/buildings/<donor>.mtl
```

An `INCOMPLETE` building is fatal. Test donors against the exact supported game
version.

Use ASCII Tesmio tokens and UTF-8 without BOM for INIs. Display names may use
Unicode where the source format supports it.

## Deposit richness

Each deposit may adjust only the initial generation of its own channel:

```ini
[natural_gas]
token = $TYPE_MINE_NATURAL_GAS
radius = oil
icon = naturalgas
richness_offset = 0.03
```

`richness_offset` accepts a finite decimal from `-0.25` through `+0.25`. It is
added to the fractal field before the fixed threshold:

- positive values produce larger, richer deposits;
- negative values produce smaller, weaker deposits;
- omitted or `0.00` preserves the original distribution.

Invalid values block startup before confirmation. Changing the setting does
not regenerate a channel already recorded in a world's manifest; this protects
saved painting and depletion. The new value applies to new worlds or a deposit
installed later into an unused channel.

## Assets

Place files below `assets/media_soviet/...`. SML copies only changed bytes to
the resolved VFS. When an asset disappears from a mod, SML removes its staged
copy only if it still matches the last hash written by SML, preserving local
edits.

Avoid publishing the same path as another mod. The later mod wins
deterministically and the conflict appears in the confirmation details.

## Native hooks

A hook is an ordinary TesmioLoader plugin listed under `[hooks]`. Compile for
Windows x64 with the static runtime and export:

```cpp
extern "C" __declspec(dllexport) uint32_t TsmPluginApiVersion();
extern "C" __declspec(dllexport) int TsmPluginInit(
    const TsmHost* host, TsmPluginInfo* info);
extern "C" __declspec(dllexport) int TsmPluginStart(); // optional
```

Use `Init` to validate the host, read configuration, and publish services. Use
`Start` for hooks that depend on services from other plugins. Check
`host->structSize` before reading API 4 additions such as `vfsRoot`.

Native patches must verify the game version and original prologue bytes. Never
reuse executable addresses across game versions without revalidation. See the
[upstream plugin guide](https://github.com/MaxLegend/TesmioLoader/blob/master/docs/09-plugins.md)
and [public API](https://github.com/MaxLegend/TesmioLoader/blob/master/src/tesmio_api.h).

## Release checklist

1. Use the final permanent mod ID from the first test.
2. Develop outside SML's reserved `9100000000..9199999999` WIP range.
3. Confirm load order and conflicts in the SML dialog.
4. Review `tesmioloader.log` and `soviet_mod_loader/report.json`.
5. Test new game, save, reload, and later installation of new content.
6. Test missing dependencies and incompatible versions.
7. Test alongside mods that declare similar content.
8. Verify resource registration, deposit acceptance, texture loading, and
   building completeness separately.
9. Publish only source mod files, never local catalogs or generated DDS files.

Adding or removing resources, needs, buildings, or deposits can affect
serialized data. Document save compatibility and recommend backups.
