# Roadmap and honest status

| Phase | Goal | Status |
|---|---|---|
| 0 Spike | bbport integration | **Patch written and verified as far as possible without the game**: compiles with bbport's `-Werror` flags against real SDL3/ImGui, applies cleanly to bbport 1.9, and a Windows smoke test (bbport's own glue + `bb_longjmp` + real ImGui + real mod DLLs, under Wine) passes. **Needs the game:** a full `./build.sh`, and confirming `scePadReadState` runs once per frame (`BB_MODS_DEV=1` prints the rate). |
| 1 Host core | Loader, registry, ABI, patches | **Done.** ABI 1.1 adds input and widgets. 1,000-reload soak on Linux and Windows (Wine). |
| 2 Hooks + hot reload | Hooks, watcher, state, fault isolation | **Done and tested:** inline hooks under live callers (real `SuspendThread` on Windows), watcher, state hand-off, crash isolation for every host→mod call (real crashes on Linux, `bb_longjmp` path on Windows), stuck-thread timeout, thread-safe with the overlay (ThreadSanitizer clean: 300 reloads, 58k concurrent draws). |
| 3 Rust SDK | Crates, template, samples | **Done:** `detour!` (with panic fallback), `hook_inline_raw`, input, typed memory, pointer chains, widgets; template with `dev.ps1`/`dev.sh` watch loop; examples `inspector`, `devtools`, `live-patches`, `hello-rust`, all tested on Linux. Windows-target Rust build not tried here (no Windows Rust toolchain in this sandbox). |
| 4 Live assets | Overlay, XML, `file_override` | **Done in host and wired into `runtime_file.c`.** All 62 entries of bbport's real `Bloodborne.xml` apply and revert. |
| 5 Multiplayer | ModNet | Host side done and tested over loopback; **not wired to bbnet** (needs shadNet rate limits, open questions 2 and 3). |
| 6 C# bridge | hostfxr | Not started. |
| 7 Release | Upstream PR, docs | Docs written (MODDING.md). F1 mods overlay and `bbmods.log` added to the patch. Upstream PR not started. |

## Bugs the tests found (all fixed)

1. `order_enabled` was `static` but called from another file: the library did not link.
2. `hook_inline` published the trampoline pointer to the mod *after* patching the target. A
   thread could enter the detour first and call through a null pointer (crash within a few
   reloads under load). Now published before the patch.
3. Nothing checked for a thread inside a mod's DLL before it was unmapped (the in-flight
   counter cannot see a detour's first and last instructions). `mh_unload` now requires the
   DLL range to be clear and leaves the library mapped instead of risking a crash.
4. Session-scope channels accepted data from peers whose handshake had not completed.
5. A faulted mod's `FAULTED` state was overwritten by the queued unload, so the overlay would
   have lost the fault. It now stays until the mod is reloaded.
6. Every unload bumped `reload_count`, so plain unloads looked like reloads.
7. Windows layer: `LoadLibraryEx` with `LOAD_WITH_ALTERED_SEARCH_PATH` was given a possibly
   relative path (undefined behaviour); `plat_mkdirs` never created the first component of a
   relative path.
8. bbport draws its overlay on a different thread from the game: a hot reload could free a mod
   while its panel was drawing, and network callbacks ran on bbnet threads. Fixed with a
   recursive run lock (overlay only try-locks) and a queue drained at the sync point.
9. Real `Bloodborne.xml` entries (the FPS patches, Performance Patch) overwrite their own bytes
   line by line; the host rejected that as a conflict. Lines of one entry may now overlap.
10. The file overlay was read by file threads while the game thread rebuilt it, and a code
    mod's own DLL/`mod.toml` were registered as game files.
11. A reloaded DLL could reuse a still-mapped shadow copy's name; shadow names are now unique
    and stale ones are cleared at start.
12. `detour!` could not see `original` (macro hygiene); `restore_state` had no `Cx`, so a mod
    could not re-apply its patches after a hot reload.
14. Generated param layouts were one byte long for items, enemies and four more tables:
    `dummy8` padding bits were packed into a new byte instead of the preceding `u8` bits.
    Caught by checking every computed layout against the row stride measured in Openborne's
    dumps (45 of 63 now verified that way; the rest are checked against memory at runtime).
13. `scan` could not continue past a hit (`start>0, len=0` was rejected), so a mod could not
    enumerate every match; it now runs from `start` to the end of the image.

## Next steps, in order

1. **You, with the game:** apply the patch, `./build.sh`, start with `BB_MODS_DEV=1`, check the
   console for `Mods: N in ...` and the sync-point rate, open the Mods window, try `devtools`.
2. Use the **inspector** in game (REVERSING.md): confirm Params finds the tables (the header
   layout is verified against Openborne's dumps, not yet against the running game), then map
   the player/enemy/inventory objects; collect findings in a shared `bbmod-bloodborne` crate.
3. Hardware watchpoints in the host ("what writes this address"): debug registers on every
   game thread + routing `EXCEPTION_SINGLE_STEP` from bbport's handler.
3. Hook one real game function with `detour!` and soak it in real play.
3. Wire ModNet to bbnet once the shadNet limits are known.
4. Linux bbport build (`build.sh` links `bb-probe` without CMake).
5. C# bridge; upstream PR.

## Open questions (from the spec)

1. Will bbport maintainers accept the host upstream plus a plugin licence exception?
2. What rate limits do shadNet / The Hunter's Dream allow for room messages?
3. Can a room attribute carry the mod-set hash, or does it need server changes?
4. Cleanest game-side trigger for "reload area"?
5. Safe per-frame point on the main game thread? (see INTEGRATION.md)
6. Compare DLL hashes by default, or only `id` + `protocol`?
