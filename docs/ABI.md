# ABI rules (v1.1)

`sdk/include/bbmod.h` is authoritative. Rules:

1. Plain C types only. No CRT objects, `std::` types or Rust `String` cross the boundary.
2. Whoever allocates frees. Blobs given to the host use `host->alloc`.
3. `BBM_ABI_MAJOR` changes break mods. `BBM_ABI_MINOR` only **appends** fields to the end of
   `bbm_host`; check `host->size` before using a newer field.
4. Host functions use the platform C calling convention (Win64 on Windows). **Only detours
   that GAME code calls are `sysv64`.**
5. Every resource is owned by the mod handle and released on unload. `release()` is optional.
6. Callbacks run on the game's main thread at the sync point unless stated otherwise.
   `thread_spawn` threads must watch the `stop` flag.
7. Never keep a `bbm_handle` or `bbm_host *` in a global that survives your DLL.
8. Every call the host makes into a mod runs under a fault guard where the integrator provides
   one (bbport does): a crash disables that mod only.

## 1.1 additions

Input (`key_down`, `key_pressed`, `pad_buttons`, `pad_pressed`; sampled once per frame) and
widgets (`ui_slider_int`, `ui_input_text`, `ui_collapsing_header`, `ui_combo`,
`ui_text_colored`). They sit after `session_info`; a mod checks `host->size` before using one
(the Rust SDK does it for you). 1.0 mods keep working.

## Mod manifest (`mod.toml`)

```toml
[mod]
id = "author.name"          # letters, digits, . - _
name = "Display name"
version = "1.2.3"
abi = "1.1"
dll = "name.dll"
runtime = "native"          # "dotnet" once the bridge exists
after = []                  # ids to load after (optional)

[load]
hot_reload = true
enabled = true

[multiplayer]
scope = "client"            # client | session | host-authoritative
protocol = 1
```
