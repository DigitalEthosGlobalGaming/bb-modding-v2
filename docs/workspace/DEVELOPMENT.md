# Development workflow

Use [MODDING.md](../MODDING.md) for project creation, canonical SDK dependencies,
building and tests. [RUNTIME.md](../RUNTIME.md) covers runtime preparation and
explicit deployment with rollback. Retained projects stay in `../modding/mods`
relative to the toolkit; new projects use ignored `mods/` by default.

Builds must leave installed versions untouched. Every mod needs a unique manifest
ID, ABI requirement and DLL name. Keep panic unwinding and package its manifest.
Preserve installed configuration and retain rollback copies during deployment.

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
