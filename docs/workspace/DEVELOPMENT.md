> Historical workspace notes. Paths and validation describe the original workspace; use ../SETUP.md for this toolkit. Inspector and bulk recovered tables are excluded.

# Create or modify a Rust mod

Use PowerShell from `<workspace>`, then load the installed tools:

```powershell
. .\modding\dev-env.ps1
```

Create source under `modding/mods/<your-mod>`, outside the watched installation.
Copy the Rust template from
`modding/bloodborne_pc/third_party/bbmod/templates/rust-mod`, or use the paired
hooks test mods as a complete example. Adjust its SDK path for the new directory.
A crate directly under `modding/mods/<your-mod>` uses:

```toml
[package]
name = "my-mod"
version = "0.1.0"
edition = "2021"

[lib]
crate-type = ["cdylib"]

[dependencies]
bbmod = { path = "../../bloodborne_pc/third_party/bbmod/sdk/rust/bbmod" }

[profile.release]
panic = "unwind"
```

Implement `bbmod::Mod::load`, register owned callbacks/panels, and export with
`bbmod::export!(MyMod)`. Give every mod a unique manifest `[mod] id`, name, version,
ABI and DLL name. Use the minimum ABI your features require; match the new shared
hooks examples when using that extension. DLL filenames replace Cargo package
hyphens with underscores. Declare `[load] hot_reload = true` for development.

Build a package containing the manifest and DLL. Build failure must leave the
installed version untouched. New mods are discovered at host sync points about
every five seconds; prepare an entire new folder outside `bbmods`, then move it
into place. Replace an existing DLL atomically on the same volume, retaining a
rollback copy. Preserve existing manifest settings and user configuration at
`bbport-windows/config/<mod-id>.toml`; external values override packaged defaults.

Launch `modding/Play Modded Bloodborne.cmd`. F1 opens the overlay. The launcher
enables development reloads; manual Reload is also available in the Mods panel.
Use `bbport-windows/bbmods.log` for load failures and event diagnostics.

## Lifecycle and threads

`load` registers the new instance. `save_state`/`restore_state` can transfer small,
versioned logical state such as counters; never persist heap addresses, host
resource handles or pointers into a DLL. Recreate resources after reload.

Host callbacks and panels are serialized by the host lock. A panel runs on the
present thread, so do not assume it is an appropriate native game execution point.
Queue requests through the documented service instead. Low-level detours can run
concurrently on game threads; use atomics and obey the exact SysV ABI. Prefer
shared typed services over each mod installing a detour on the same address.

Use `cx.spawn` for background work and check its stop flag. Do not create unmanaged
threads, TLS destructors, or globals retaining `Cx`/resource handles. Host-owned
registrations are removed on unload or recovered fault. Ordinary `cx.write` and
`cx.write_val` are **not** reverted. Do not use reversible code patches as a generic
rollback mechanism for player heap fields whose owner may disappear during loading.

## Scope and evidence

The host overlay already supports separately owned panels from multiple mods.
This does not provide insertion into the game's native menu/widget tree. Likewise,
input presses are input observations, not proof that a weapon attack succeeded.
Report a field's owner, layout version, producer and validation status explicitly.
