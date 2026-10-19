# Architecture

All of this lives inside `bb-probe.exe`. Guest (PS4) pointers are host pointers, so hooks
and patches work on the live process image.

```
game fn --jmp--> host gate --(mod active?)--> mod detour --> trampoline --> rest of game fn
                      '--------(bypassed/draining)------------------------> trampoline
```

## Components

* **Registry / loader** (`loader.c`, `host.c`): discovers `mods/*/mod.toml`, orders by
  `after`, shadow-copies the DLL into the cache (Windows locks loaded DLLs), calls
  `bbmod_entry`, then `on_load`. Owns a per-mod resource list.
* **Patch manager** (`code.c`): byte patches with original bytes saved, overlap/conflict
  checks across mods, reverted in reverse order on unload.
* **Hook engine** (`code.c`): inline hooks (Zydis relocates stolen bytes into a trampoline)
  and slot hooks (vtables, function tables). A gate redirects to the trampoline while a mod
  drains, so unload never leaves a thread inside unmapped code.
* **XML patches** (`xmlpatch.c`): shadPS4/GoldHEN patch files, applied as one owned group.
* **Virtual overlay** (`vfs.c`): `modhost_vfs_resolve()` for the runtime file open path;
  mod folders and `file_override` entries share one table.
* **ModNet** (`net.c`): channels, mod-set hash, per-peer token-bucket budget.
* **Sync point**: `modhost_frame()` runs on the game's main thread once per frame. All reloads,
  watcher checks and frame callbacks happen there.

## Unload sequence (`mh_unload`)

1. Mark the mod bypassed; hooks route to trampolines.
2. Drain: wait until the in-flight counter is zero (default timeout 500 ms; else report stuck).
3. `on_save_state` (reload only).
4. `on_unload`, then revert every owned resource, then `destroy`.
5. Quiesce: with game threads suspended, check no thread's instruction pointer is inside the
   mod's DLL (the in-flight counter cannot see a detour's first and last instructions). Only
   then `FreeLibrary`. If a thread will not leave, the DLL is left mapped and a warning logged.

Install order matters too: the trampoline pointer is handed to the mod **before** the target
is patched, because another thread may enter the detour the instant the jump is written.

Known limit: a slot hook (vtable entry) can be racing with a thread that already loaded the
detour address from the slot but has not called it yet. The quiesce check narrows this; a
deferred unmap would close it (not built).

## Hot reload

Watcher debounces 250 ms on DLL mtime/size, then at the next sync point: save state, unload,
shadow-copy the new DLL, load, restore state. Failure leaves the mod `Faulted` with
`last_error` shown in the overlay; the game keeps running.

## Fault isolation

The exception handler calls `modhost_on_fault(ip)`. If `ip` is inside a mod's DLL range the
mod is bypassed and marked `Faulted`; bbport then recovers however it already recovers.
