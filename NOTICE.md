# Upstream compatibility notice

This project targets and mirrors the public API v4 POD layout from
[MaxLegend/TesmioLoader](https://github.com/MaxLegend/TesmioLoader), licensed
under GPL-3.0. It follows the upstream ordinary-DLL discovery, `/MT`, two-phase
`TsmPluginInit`/`TsmPluginStart`, service registry, and soft-failure patterns.
The four content capabilities are intentionally linked into one SML plugin
rather than distributed as four standalone DLLs.

The synchronization baseline is upstream commit
`3baa141f9f08921aea9c95f0a400289cabd9960a` (TesmioLoader b0.3.6). The
`vendor/tesmio` directory incorporates `buildings`, `resources`, `deposits`,
and `needs`. They are compiled with renamed, non-exported entry points and
linked into the Soviet Mod Loader DLL. `deposits` additionally contains the
SML's universal per-game generation and persistence logic. `buildings` remains
bundled even though its standalone upstream build is currently parked.

No game executable addresses or new binary patches are introduced by the
manager itself. Native Workshop hooks remain responsible for using upstream's
verified hook mechanisms and for matching the installed game version.
