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
| Existing personal projects | Retained ../modding/mods and ../modding/bb-modding-adaptive-triggers |

Existing projects use the canonical SDK/scripts; duplicate toolkit copies were archived
and removed. Prefer .\bb.ps1 build-mod <project> --test and toolkit content commands.
The independent adaptive-trigger repository retains its pinned Git dependency.
The Mod Manager specification remains a plan, not an implemented package manager.

Old repositories and trial outputs are now verified private source-history archives,
not active checkouts. See CONSOLIDATION.md for recovery locations and current workflows.
Inspector/Openborne schemas, private disassembly, game assets and generated catalogs
remain excluded from shareable source. Smithbox PARAMDEF XML is downloaded locally.
Existing private curated NPC/team labels are preserved under .local/reference-inputs;
the catalog generator validates their hashes/game fingerprints before applying them.
These inputs are optional and are not distributed by the toolkit.
