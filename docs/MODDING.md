# Native mod development

Run setup.ps1 first, then create an independent project:

```powershell
.\bb.ps1 new-mod me.my-mod "My Mod"
cargo build --manifest-path mods/me.my-mod/Cargo.toml --release
```

The starter depends on this checkout's SDK and uses panic unwinding. Source lives under
ignored mods/, so give each project its own repository. Reviewed examples are under
docs/examples/; build with cargo build -p hello-rust. Inspector is excluded.

Selecting a game installation does not install the host. Read RUNTIME.md before deploying.
With a compatible host installed, close the game/launcher and run:

```powershell
.\bb.ps1 deploy-mod mods/me.my-mod
```

This builds the release DLL, deploys it with a rollback backup and preserves an existing
installed manifest. It installs mod.toml on the first deployment. Content overlays require
separate build/deployment planning. F1 opens the integrated overlay; BB_MODS_DEV=1 enables
DLL hot reload in the launch environment. Host diagnostics go to bbmods.log.

Use host-owned hooks, callbacks, worker threads, panels and overrides. Never retain host
pointers across reloads or allow panics across the FFI boundary. Check capabilities and
handle Unsupported: newer SDK services need matching port backends.

Enable the SDK dependency's local-catalogs Cargo feature to use locally generated names.
Without it, catalog lookups return unknown and the SDK builds without game data.
Read RUST_SDK.md, ABI.md and sdk/include/bbmod.h for exact interfaces.
Synthetic tests establish API/ownership behavior, not gameplay semantics.

Retained workspace projects stay at `../modding/mods/`; the independent
`../modding/bb-modding-adaptive-triggers/` repository retains its pinned Git SDK.
Build an explicit project from this toolkit without deployment:

```powershell
.\bb.ps1 build-mod ../modding/mods/character-observer --test
.\bb.ps1 build-mod ../modding/mods/hooks-testing-mod --test
.\bb.ps1 build-mod ../modding/mods/adaptive-triggers
```

Local retained projects depend directly on `sdk/rust/bbmod` with `local-catalogs`.
This installation retains its curated names/team dictionaries in ignored
`.local/reference-inputs/catalogs`. `generate_catalogs.py` reapplies those snapshots
only after verifying every recorded payload hash, the CUSA03173/1.09 version and
all five original game-source fingerprints. Their original source/licence records
remain alongside them. Fresh installations without those private inputs explicitly
report unavailable curated names; no private dictionary ships in public source.
Set `MSYS2_ROOT`, `WINPYTHON`, or `BBPORT_SOURCE` to override toolchain/runtime source
choices. Native adaptive backend tests require the prepared canonical port checkout.
Observer script references use `game-unpacked/decompiled`; configure the mod's
script root or `BB_REFERENCE_DIR` to override it. Rebuild its private links with:

```powershell
pwsh -File src/scripts/export-map-script-placements.ps1 -Output ../modding/mods/character-observer/data/map-placements.json
.\bb.ps1 script-links --mod ../modding/mods/character-observer
```

The generator joins raw maps/PARAM/LUAINFO with the separate decompiled source
folder. It writes only the selected mod's generated table and provenance. These
are baseline associations; they do not prove a live AI call or script binding.
