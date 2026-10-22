# Rust SDK (`sdk/rust/bbmod`)

Start with `bb.ps1 new-mod`; MODDING.md has the toolkit workflow.
Local catalogs require the `local-catalogs` Cargo feature. Optional private curated
references are reapplied from hash-verified `.local/reference-inputs/catalogs` when present. Newer services require matching runtime backends; see RUNTIME.md.
For this workspace, start with the maintained
[developer documentation](workspace/README.md), including its
example `AGENTS.md` and two shared-hook test mods.
Use local game-unpacked reports to find recovered scripts. Archive member basenames are retained.

## The `Mod` trait

```rust
impl bbmod::Mod for MyMod {
    fn load(cx: &mut Cx) -> Result<Self>;                     // register everything here
    fn unload(&mut self, cx: &mut Cx) {}                     // optional; resources revert anyway
    fn save_state(&self) -> Option<Vec<u8>> { None }          // hot reload hand-off
    fn restore_state(&mut self, cx: &mut Cx, state: &[u8], from: Version) {} // after load; re-apply here
    fn event(&mut self, cx: &mut Cx, ev: &Event) {}           // see cx.subscribe
}
bbmod::export!(MyMod);
```

## `Cx` at a glance

| Area | Calls |
|---|---|
| Log, config | `log_info/warn/error/debug`, `info!(cx, ..)`, `config("sec.key")`, `config_parse::<T>`, `config_bool` |
| Addresses | `eboot_base()`, `ps4(addr)` / `ps4_to_host`, `scan(pattern, 0, 0)`, `follow(base, &[offsets])` |
| Memory | `read`, `read_val::<T>`, `read_ptr`, `write`, `write_val` (game data, not reverted) |
| Patches (reverted) | `patch(addr, bytes)`, `patch_val(addr, v)`, `patch_xml(path, Some(name))`, `release(id)` |
| Hooks (removed) | `bbmod::detour!` + `name::install(cx, addr)` / `install_slot`; raw `hook_inline`, `hook_inline_raw`, `hook_slot` |
| Every frame | `on_frame(|cx| ..)` on the game thread |
| Dynamic registration | `on_frame_mut(|cx| ..)` provides exclusive SDK access at the serialized callback for registering owned closures |
| Input | `key_down`, `key_pressed` (`bbmod::key::*`, SDL scancodes), `pad_down`, `pad_pressed` (`bbmod::pad::*`) |
| UI | `panel(title, |ui| ..)`: `text`, `text_colored`, `button`, `checkbox`, `slider_f32`, `slider_i32`, `input_text`, `combo`, `collapsing_header`, `separator`, `same_line`, `ui.cx()`; `notify(msg)` toasts |
| Files | `file_override("dvdroot_ps4/..", source)` |
| Threads | `spawn(|stop| while !stop() { .. })` |
| ModNet | `channel(name, flags, |cx, peer, bytes| ..)`, `send`, `broadcast`, `session()` |

ABI 1.1 calls (input, new widgets) read as released / do nothing on a 1.0 host.

## Safety rules

* Callbacks and panels are guarded: a crash or panic there disables only your mod.
* A detour runs on game threads, concurrently with everything: keep shared state in atomics.
  `detour!` adds the in-flight guard and turns a panic into a call to the original.
* No threads of your own (use `cx.spawn`), no thread-local destructors, no `static` holding
  `Cx` or host pointers (a `static Detour` is fine).
* Build for x86-64 Windows (MSVC or GNU). Keep `panic = "unwind"` (the template does).

## ABI 1.3: DualSense pad effects

`bbmod::dualsense` provides `off`, `resistance`, `resistance_vibration`, and experimental `raw` effect builders, plus `Trigger::{Left,Right}`. `Cx::pad_info`, `pad_trigger`, `pad_release`, and `pad_lightbar` check table size and return unsupported on an older host. Trigger/lightbar claims are host owned and released on unload or a recovered mod fault. The host suppresses effects during backend input capture. Physical trigger depths are independent of game action remapping.

User configuration at `<mods_dir>/../config/<mod-id>.toml` is checked before the packaged manifest for each config key. Missing keys fall back to manifest defaults.

## ABI 1.4: shared gameplay and UI services

`bbmod::gameplay` exposes typed player snapshots and sampled changes;
`Cx::player_adjust` performs explicit guarded HP/stamina adjustments, and
`Cx::player_modifier` registers owned deterministic scaling claims for those
requests. Claims compose across mods and disappear on owner unload. They do not
intercept native damage or recovery. UI callbacks should queue adjustment requests
for `on_frame`. Writes are optimistic against game workers and have no automatic undo.

`bbmod::equipment` exposes experimental equipment snapshots/change observations
and native item-use results via `Event::item_use()`. Equipment indices/references
are diagnostic, without verified hand names. Item-use reports a native action
result, not an input press or general weapon attack; it is read-only and bounded.
`Event::attack_observed()` reports limited native behavior/attack paths with a
behavior ID and source. It does not identify the weapon or hit target, cannot
intercept damage, and may report one action through multiple sources.

`bbmod::inventory` supplies a bounded, shared carried-inventory cache. Use
`Cx::inventory_page(revision, offset, limit)` or `Cx::inventory_snapshot()` for
consistent copies and `Event::inventory_change()` for sampled summary changes.
Pages contain up to 64 occupied entries, with a 4096-slot cap and a 250 ms maximum
sampling frequency. Use encoded IDs only when `ID_RESOLVED` is set; storage and
inventory writes are outside this API.

`Cx::contribute(slot, key, order, draw)` allows multiple mods to contribute to one
overlay window with isolated widget IDs and deterministic ordering. Use stable
letters/digits/dot/underscore/hyphen names; cleanup affects only the owner's content.
This is ImGui overlay composition, not native game-widget injection/interception.

See [the shared-service contract](workspace/SHARED-HOOKS.md) and
the implementation for exact capability checks, limits and remaining research.

## ABI 1.5: compiled content and explicit item awards

`bbmod::content` provides typed declarations, a generated registry with
`namespace.name` identities, tags, item types and exact carried-inventory lookups.
Declarations are compiled offline into a merged file overlay; they do not insert
new parameter rows into a running game. Persistent numeric allocations belong
to the content workspace's `item-ids.lock.json`.

`Cx::give_content_item` and `Cx::give_item_raw` queue bounded item awards at a
fingerprinted native update point. `Cx::item_give_status` consumes terminal
results. Submitted means the native routine ran, not confirmed inventory or save
insertion. Requests validate the current player generation and loaded item row;
pending requests cancel on unload, while running requests cannot be cancelled.
These appended ABI functions return unsupported on older hosts.

`CombatHooks::on_attack` dispatches the existing limited behavior observations;
`supports_hit()` is false. See [content SDK details](workspace/content-sdk.md)
and [the three content examples](../../modding/mods/item-examples/README.md).

## Optional USB grip-haptics extension

`Cx::pad_haptics(&[[left, right], ...])` submits a finite 48 kHz stereo actuator
clip. Maximum 12000 frames (250 ms), finite samples within [-0.5, 0.5]. Empty data
stops and releases ownership. Calls copy synchronously; the host retains no mod
buffer/callback. One mod owns grip output; competing calls return conflict.
The extension is appended after `item_give_status` and must be gated by table
size, not ABI minor alone. Older hosts return unsupported; existing pad calls
use their own field-size boundaries so trigger support remains available.

The bbport backend requires a single USB PS5 controller and a unique matching
quad audio endpoint (name/layout heuristic). Front audio channels are zero.
Capture, disconnect, unload and recovered faults close output. Clips replace
rather than accumulate and finish even if game polling stalls. This API supplies
output only; it provides no floor material, footstep, hit or locomotion event.

## ABI 1.6: character observations

`Cx::characters()` copies the shared loaded-character cache. Subscribe to
`characters::CHANGED` and decode `Event::characters_changed()`; use a
`CharacterTracker` for per-character observations. Flags gate HP, NPC IDs and
raw script values. These are sampled observations, not spawn/death/damage or AI
decision callbacks. The host reads bounded native character sets every 250 ms
when needed. The new function is appended after USB haptics; older table sizes
return unsupported. See [enemy/boss and AI docs](workspace/CHARACTERS-AND-AI.md).

## ABI 1.7: sortable tables

`Ui::table(id, headers, height, &mut sort_column, &mut descending)` opens a
scrolling/resizable table and returns current single-column sort preferences
before the mod draws rows. Sort the data in the mod, then call the returned
table's `next_row()` and `next_column()` around normal UI text/widgets. Dropping
the table closes it, including during Rust unwind. The host also closes an
unfinished table when a draw callback returns or faults. One table may be open
per mod draw, with at most 32 columns. Table calls are valid only in the owning
draw callback. Older hosts/backends and clipped tables return None. The ImGui
backend freezes the first column/header and isolates IDs using existing scopes.

## Item and character definitions

`items::by_id` accepts category-encoded IDs; `by_param_id(category, id)` supplies
the explicit category for raw row IDs. Inventory references require resolution
before either lookup. `bbmod::get_character_definition(npc_param_id)` returns
baseline NPC metadata, including optional display/description text and internal
row names. `CharacterExt::definition`, snapshot `by_definition`/`by_handle`, and
`CharacterRef` connect definitions to read-only observed instances. See
[definitions and ID semantics](workspace/CHARACTER-DEFINITIONS.md).

## Persistent settings and data: `bbmod::store`

Any mod can keep settings and private data across runs with `bbmod::store::Store`. It is
pure `std` (no host ABI change), so it works with an already-installed runtime.

| Where | What |
|---|---|
| `<bbmods>/../config/<mod-id>.toml` | Settings. The same user-override file the host reads for `cx.config`, so saved values are read back by `cx.config("section.key")` on the next load. |
| `<bbmods>/../mod-data/<mod-id>/` | Private blobs (`read_blob` / `write_blob` / `remove_blob`). |

```rust
let mut store = bbmod::store::Store::open(cx)?;          // id from mod.toml, dir from BB_CODEMODS_DIR
let level: u32 = store.get_parse("difficulty", "level").unwrap_or(2);
store.set("difficulty", "level", 3);                     // numbers/bools; set_str quotes text
// every frame:
store.tick()?;                                           // saves after the value settles (1 s)
if store.poll_external()? { /* a tool or editor changed the file: re-read values */ }
```

* The file is plain TOML (`[section]`, `key = value`). Changing a value preserves comments,
  unrelated keys and other sections; an unchanged value does not rewrite the file.
* Writes are atomic (temp file then rename) and debounced, so dragging a slider is cheap.
  Pending changes are also written when the `Store` is dropped (mod unload).
* `poll_external` lets another UI or a text editor change settings live. It does nothing
  while local changes are unsaved, and it ignores the store's own writes.
* Ids and blob names are limited to letters, digits, `.`, `_`, `-` (no path separators,
  no leading dot). `Store::open_at(root, id)` uses an explicit root, e.g. for tests.
* Values are not secret and not cross-checked: treat the file as user input.
