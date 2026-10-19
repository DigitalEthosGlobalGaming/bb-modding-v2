# bb-modding

A Windows development toolkit for native bbmod DLLs and Bloodborne content mods.
Clone the source, select your own game and installed port, and generate your private
reference workspace. Game files and recovered game code are not distributed here.

The latest local native-menu/text-entry work is consolidated here. See
[development consolidation](docs/CONSOLIDATION.md) for the preserved port WIP,
legacy tools/references and separate mod project locations.

## Start

Open PowerShell in this directory:

```powershell
.\setup.ps1
```

Two folder pickers ask for the **game parent folder** (contains
`CUSA03173/uroot`) and your installed **bbport folder** (contains `Bloodborne.exe`).
Setup validates European GOTY CUSA03173 update 1.09, installs pinned Smithbox and
DSLuaDecompiler, then extracts DCX/BND4 references and decompiles Lua/HavokScript.
Allow at least **60 GiB free** for full extraction; the previous export used about 52 GB.
Paired texture/animation archives and opaque formats remain available in the original
game tree; this is a reference export, not a universal conversion of every game format.

Missing prerequisites? Use `.\setup.ps1 -InstallPrerequisites`. The script shows system
installers before running them. See [setup details](docs/SETUP.md) for unattended use,
smaller extraction, reruns and recovery.

```powershell
.\bb.ps1 doctor
.\bb.ps1 smithbox
.\bb.ps1 new-mod me.my-mod "My Mod"
cargo build --manifest-path mods/me.my-mod/Cargo.toml --release
```

Setup does not deploy anything. A stock runtime does not gain framework support merely
by selecting it. See [runtime integration and rollback](docs/RUNTIME.md).

## Layout

| Directory | Contents | Source control |
| --- | --- | --- |
| `tools/` | Pinned downloads, tool source, binaries and optional port build | Ignored |
| `game-unpacked/raw/` | Preserved decompressed archives and archive members | Ignored |
| `game-unpacked/decompiled/` | UTF-8 recovered scripts with original basenames | Ignored |
| `mods/` | Your independent mod projects | Ignored |
| `.agents/` | Toolkit agent skill | Tracked |
| `src/host/` | Framework host | Tracked |
| `src/scripts/` | Setup, generation, content and deployment tools | Tracked |
| `src/integration/`, `src/tests/` | Port integration and synthetic tests | Tracked |
| `sdk/` | C ABI and Rust SDK | Tracked; generated local data ignored |
| `docs/examples/`, `docs/templates/` | Reviewed source examples and starters | Tracked |
| `.local/` | Machine paths, reports, build stages and rollback backups | Ignored |

The SDK compiles without any game catalogs. To use your generated private lookups, enable
its `local-catalogs` Cargo feature. Generated instruction profiles are ignored `*.local.h`
files; without them, the affected native services fail their compatibility checks.

## Guides

- [Setup and troubleshooting](docs/SETUP.md)
- [Native mods and current SDK](docs/MODDING.md), [Rust API](docs/RUST_SDK.md)
- [Content overlays](docs/CONTENT.md)
- [Runtime build/deploy/rollback](docs/RUNTIME.md)
- [Agent starter prompt](docs/STARTER-PROMPT.md)
- [Source provenance and publication boundary](docs/PROVENANCE.md)
- [Migration inventory](docs/MIGRATION.md) and [validation](docs/VALIDATION.md)
- [Licensing](LICENSING.md)

Before committing or packaging:

```powershell
python src/scripts/audit_publication.py
python -m unittest discover -s src/tests/toolkit -v
cargo test --workspace --locked
```

Compatibility checks and synthetic tests are not gameplay validation. The pinned runtime
integration provides the older overlay/input/reload/override bridge; newer SDK services
still require matching port backends. Inspector/Openborne schemas are excluded.
