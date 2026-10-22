# Development workspace consolidation — 20 October 2026

`bb-modding` is the active toolkit root. Framework source lives in `src/host`, the SDK
in `sdk`, scripts in `src/scripts`, examples/templates in `docs`, and agent guidance in
`.agents`. Run toolkit commands here. Existing independent projects stay in
`../modding/mods/` and `../modding/bb-modding-adaptive-triggers/`; new projects can use
ignored `mods/` or their own repository. Duplicate project copies have been retired.

## Daily work

```powershell
.\bb.ps1 doctor
.\bb.ps1 build-mod ../modding/mods/character-observer --test
.\bb.ps1 content verify
.\bb.ps1 native-assets
.\bb.ps1 script-links --mod ../modding/mods/character-observer --check
.\bb.ps1 map-placements --output .local/map-placements.json
```

PowerShell 7 is preferred for build helpers and required for map placement export.
The toolkit builds/packages retained projects without deployment. Keep generated catalogs,
overlays, reference exports and machine settings private. Preserve the existing content
item-ID allocation lock in `.local/content-workspace` and curated reference snapshots in
`.local/reference-inputs`; generators validate provenance before applying private labels.

## One runtime source workflow

`tools/bloodborne_pc` is the only active port checkout, pinned at
`b3fc78bff0997f75f73e14a5fffeb1141f0cea70` through `src/scripts/tools.lock.json`.
The authored `src/integration/bbport-modhost.patch` brings forward the current bridge;
preparation copies canonical host/SDK source and records hashes, rejecting edited copies.
Edit canonical source and use the documented prepare/build workflow in RUNTIME.md.
ABI 1.10 native services remain profile/capability gated. The bridge includes shared
input/text, overlay/tables, reload, overrides and pad effects/USB haptics.

The old WIP merge conflicts were resolved for integration; its uncommitted merge and Git
metadata were archived, with the original conflict stages/index separately preserved.
There is no active WIP checkout or compatibility junction tree. The installed runtime
remains unchanged; a separate staged runtime is available under `.local/runtime-stage`.

## Retired material and recovery

Verified private archives under `.local/archive/source-history/` preserve:

| Archive | Contents |
| --- | --- |
| `port-wip-source.zip` + JSON manifest | Original port source, Git/index/merge state, submodule source and authored materialized framework files |
| `previous-framework.bundle` + state/settings | Complete previous clean framework Git history and repository settings |
| `previous-runtime-source.zip` + JSON manifest | Superseded pinned runtime checkout source and Git metadata |
| `previous-workspace-evidence.zip` + JSON manifest | Historical helpers, research, content state and trial outputs, including trial user data |
| `legacy-dsluadecompiler.bundle` | Complete old decompiler Git history; unique untracked files are preserved with the tool audit |
| `toolkit-mod-source-copies-20261020-verified.zip` | All 60 source/metadata files from the five duplicate toolkit mod copies |

Archive contents were read back and SHA-256 checked before source deletion. Restore into
a separate review directory, verify its manifest, and inspect historical scripts before
use; their old build/deployment paths are not current workflows. Do not overlay archives
onto the active toolkit. Original conflict snapshots are in
`.local/consolidation/runtime-20261020`; cleanup manifests are in `.local/cleanup`.

Canonical references remain in `game-unpacked/raw`, `decompiled` and `annotated`; active
pinned tools remain in `tools`. The former `game-unpacked/legacy` and `tools/legacy`
trees were removed after exact hash comparison. Approximately 49.17 GiB of duplicate
payload was identified. Seven unique reference metadata files and 295 unique tool files
(including settings, provenance and Git material) were preserved under
`.local/cleanup/duplicate-audit`. Historical WIP/build caches and duplicate mod build
outputs were also removed. Current validated build caches may be regenerated normally.

Keep `.local/archive/setup-tools`, private research, runtime rollback backups and source
history. Game packages, extracted originals, installed runtime, saves and settings remain
external inputs. The old `modding` directory now contains only the retained mod projects.

## Verification and limits

A fresh toolkit build passed 28/28 native tests, 33 Python tests, 28 Rust SDK tests and
two UI tests (three documentation examples intentionally ignored). The native suite now
builds its Rust DLL fixtures before CTest. The isolated full runtime build passed 395
build targets and 2/2 text/IME tests; its 147 staged files were hash checked.
Retained observer, hook, item and adaptive projects passed their relevant builds/tests.
Content verification, stock native assets, 2,890 map placements across 24 maps and observer
links (2,842 resolved; 524 unresolved) were checked. Unresolved links remain research gaps.
Private catalog labels and content allocations were preserved, including original output
hashes. See VALIDATION.md and private cleanup logs for actual commands and evidence.

No game deployment, launch, gameplay validation, commit or push was performed. Publication
auditing excludes private game-derived material and archives; it does not replace manual
provenance/licence review.
