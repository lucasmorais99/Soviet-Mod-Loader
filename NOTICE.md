# Upstream compatibility notice

This project targets and mirrors the public API v3 POD layout from
[MaxLegend/TesmioLoader](https://github.com/MaxLegend/TesmioLoader), licensed
under GPL-3.0. It follows the upstream folder-per-plugin, `/MT`, two-phase
`TsmPluginInit`/`TsmPluginStart`, service registry, and soft-failure patterns.

The `vendor/tesmio` directory contains the upstream `buildings`, `resources`,
`deposits`, and `needs` implementations from commit
`07f6b3e47411e04cf05429ac08e07819f94549c1`. They are compiled with renamed,
non-exported entry points and linked into the Soviet Mod Loader DLL.

No game executable addresses or new binary patches are introduced by the
manager itself. Native Workshop hooks remain responsible for using upstream's
verified hook mechanisms and for matching the installed game version.
