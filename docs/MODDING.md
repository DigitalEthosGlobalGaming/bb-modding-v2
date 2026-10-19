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
