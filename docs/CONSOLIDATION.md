# Development workspace consolidation

The toolkit is now the home of the framework and development material. Only the
separate projects under the original modding/mods directory and the standalone
adaptive-trigger repository remain outside it. The installed game, packages, runtime
user data, shortcuts and installation provenance retain their original locations.

## Current source

Authored native-menu/live-list controls, text-entry services, SDK additions and converted
example mods were brought forward into src/host, sdk and docs/examples. Catalog includes
and locally generated executable-profile safeguards were retained. Native-menu profile
updates are stored as address/length/hash metadata; actual bytes are regenerated privately.
The SDK is now ABI 1.10. This does not upgrade the pinned legacy runtime bridge.
The authored game-ui skill and its references are available under .agents/skills/game-ui.

## Preserved work in progress

The original development workspace is at **.local/work-in-progress/modding/**:

- bloodborne_pc/: complete original port checkout, including its Git metadata, staged
  changes, unstaged changes, untracked work, submodules and unresolved merges.
- bb-modding/: the previous standalone framework repository, including its history.
- Top-level development scripts, research, skills, tests, content workspace and trial outputs.

Both relocated repositories were checked against their original HEAD, Git index hash and
porcelain working-tree status; all matched. No merge was resolved or local change discarded.
The preserved port is not a clean reproducible release: finish its outstanding merge
conflicts before attempting a fresh full build. Old CMake build directories contain paths
from before relocation; configure a fresh build directory or use cmake --fresh.

Compatibility junctions inside .local/work-in-progress expose the unchanged game/runtime,
relocated tools/reference trees and retained external mod projects at the relative paths
expected by historical scripts. They do not move the game or add files to it. Avoid
recursive operations that follow these junctions. Historical deployment/launch scripts
still require an explicit request to run; none were run during consolidation.

## Other development folders

| Previous material | Location inside toolkit |
| --- | --- |
| Old tools tree | tools/legacy/ |
| Earlier unpacked/decompiled reference tree | game-unpacked/legacy/ |
| Setup utilities and private research checkouts | .local/archive/setup-tools/ |
| Existing runtime rollback backups | .local/archive/runtime-backups/ |
| Standalone test caches | .local/archive/test-caches/ |
| Source-tree metadata and previous Smithbox launcher | .local/archive/ |

Existing duplicate references/backups were retained rather than discarded. The relocation
journal, original Git-state snapshots, source import list and backups of changed external
mod path references are in .local/consolidation/. These are private machine records.

Retained local mod projects remain in their original directories. Their local dependency
and build-script references were repaired to reach the relocated original SDK/scripts.
The independent adaptive-trigger repository's pinned Git dependency was left intact.
Active content-build and latest-trial pointers were repaired, with their original JSON
saved in the consolidation records. Historical logs/reports were retained as evidence.

## Checks run after consolidation

- Rust workspace tests: 28 SDK tests and two UI example tests passed; three existing
  documentation examples remain ignored.
- CLANG64/Zydis native build and CTest: 27/27 passed, including text entry, native controls,
  converted example DLLs and host unload/reload ownership.
- Python toolkit/archive tests: 12/12 passed.
- Offline Cargo checks passed for the retained adaptive-trigger, character-observer and
  both hook mod projects against the relocated SDK.
- Regenerated executable profiles passed their address-specific verification hashes.
- Publication audit excludes the WIP repositories, archives, game references and tools.

No runtime was deployed and no gameplay validation was performed during this migration.
No new commit or push was made.
