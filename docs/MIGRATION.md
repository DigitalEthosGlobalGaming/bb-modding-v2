# Migration inventory

Original installations and source repositories were preserved. See CONSOLIDATION.md for
their current locations following workspace cleanup. This toolkit started as a fresh
source repository; the old repository histories are retained privately under .local.

| Previous work | Toolkit location |
| --- | --- |
| Framework host | src/host |
| C ABI and Rust SDK | sdk |
| Examples/templates | docs/examples and docs/templates |
| Extraction/decompilation | src/scripts/unpack_game.py and decompile_lua.py |
| Catalogs/profiles | Local generators under src/scripts; outputs ignored |
| Content compiler | src/scripts/content.py and content_pipeline.py |
| Game reference exports | game-unpacked/raw and game-unpacked/decompiled |
| Tool setup | setup.ps1 and src/scripts/tools.lock.json |
| Agent guidance | .agents/skills/bb-modding and docs/STARTER-PROMPT.md |
| Personal projects | Source-only copies under ignored mods/ |

The local migration includes adaptive-triggers, character-observer, hooks-testing-mod,
item-examples and the Mod Manager draft. Their original projects remain intact. Rust
SDK paths now point at the toolkit and opt into local catalogs. Historical per-project
build/deploy helpers may still assume the old workspace; prefer Cargo directly and
current toolkit content/deployment commands. The Mod Manager specification is a plan,
not an implemented package manager.

Inspector/Openborne schemas, private disassembly, bulk recovered tables, conflicted port
snapshots, game assets, saves and caches are excluded from the shareable source.
The content compiler now reads locally downloaded Smithbox PARAMDEF XML. Community team
and NPC reference labels are currently unavailable; numeric IDs/local game names remain.
