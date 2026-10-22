> Historical workspace notes. Paths and validation describe the original workspace; use ../SETUP.md for this toolkit. Inspector and bulk recovered tables are excluded.

# Shared services and typed hooks

ABI 1.4 adds a typed player service. Consult the actual
[`gameplay.rs`](../../sdk/rust/bbmod/src/gameplay.rs)
for signatures. The service validates the supported game layout and resolves the
current player; callers do not repeat heap scans or retain session addresses.

| Service | Meaning |
| --- | --- |
| `Cx::player_snapshot()` | Current player identity, generation, HP/max HP and stamina/max stamina. |
| `Event::player_change()` | Typed previous/current sampled player snapshots. |
| `Cx::player_adjust(resource, &snapshot, delta)` | Explicit guarded adjustment through the shared modifier pipeline; returns actual delta. |
| `Cx::player_modifier(resource, scale_milli, priority)` | Owned scaling claim for explicit adjustments. 1000 means 100%; 500 means 50%. |
| `Cx::release(id)` | Release a claim owned by this mod; unload also releases owned resources. |
| `Cx::contribute(slot, key, order, draw)` | Owned contribution to a shared overlay slot. |
| `Cx::equipment_snapshot()` / `Event::equipment_change()` | Experimental sampled equipment state, including raw references, saved IDs and selection indices. |
| `Event::item_use()` | Native item-action result with item ID and success, delivered later at a host sync point. |
| `Event::attack_observed()` | Native behavior/attack-path observation with behavior ID and source; read-only, not a unique hit or swing. |
| `Cx::inventory_page()` / `Cx::inventory_snapshot()` | Read-only carried inventory with checked revision and bounded pages. |
| `Event::inventory_change()` | Sampled inventory summary/revision change; distinct from native item use. |

`PlayerSnapshot` contains `generation`, `actor`, `component`, `health`, `max_health`,
`stamina` and `max_stamina`. Identity fields are diagnostic values, not permission
to bypass the service with raw writes. Resolve a fresh snapshot for a request.
The host rejects stale identity or expected-value mismatches. HP adjustments cannot
kill the player (minimum 1); stamina can reach zero. Both clamp to their maxima.

## Observations versus interception

Player changes are sampled at host sync points. Several game writes can occur
between samples; a decrease may represent damage, a cost or a script. A sampled
change cannot cancel native damage, distinguish rally from healing, or report every
individual native event. No generic incoming-damage multiplier is exposed here.

Scaling claims affect only calls to `player_adjust`, including calls from another
mod. They do not change natural damage, stamina consumption or recovery. Claims
compose in ascending priority, mod ID and resource ID order; do not rely on
subscriber callback order. Each multiplication rounds toward zero. Valid scales
are 0..4000; at most 64 claims are active host-wide.
Integer rounding and clamping mean a tiny requested delta may become zero.
Item-use observations come from a native action result rather than controller
input. They are read-only and cannot cancel use or change its result. Delivery is
bounded and can lose events during overflow, loading or unsupported layouts.
An item-use event is not a general weapon-attack event.
`attack_observed()` reports a separate native behavior path. Source 1 is the
verified successful behavior callsite and source 2 the verified stamina-update
callsite. It reports a behavior ID, not a weapon ID, target or hit result. One
action may produce observations through both sources; do not count these records
as unique attacks or assume every possible attack path is covered. It cannot
cancel an attack or alter native damage.
The runtime must enable its verified host-owned producer; subscribing alone does
not install a per-mod detour. The bridge filters the local player and identity
generation, preserves the original function call, and queues up to 64 records for
delivery. Native observation is disabled in safe mode and suppressed in network
sessions. Check `bbmods.log` for installation/signature status and dropped records.
Native effects, inventory awards and native menu calls still require validated
execution points and dedicated APIs before general use.

## Ownership and reload

One host service samples the player and fans out observations. Each mod subscribes
independently and must not modify the event payload. Each modifier belongs to one
loaded mod instance. Releasing or unloading one mod removes only its claims; the
other subscriber remains active. A reload starts with no claims unless that mod
explicitly opts in again. The hook examples restore their player-change counters,
never UI-enabled mutation claims; explicit configuration opt-in reapplies on load.

Submit player adjustments from a frame/event callback. The examples queue panel
button requests for `on_frame`; drawing a UI should not itself call native gameplay
functions. Handle every error, especially unavailable players during menus/loading,
unsupported layouts and stale requests. Active network sessions reject adjustments.
Expected-value checks are optimistic, not atomic against concurrent game workers.
Do not retry a failed write blindly.

## Shared overlay UI

Use a stable slot such as `hooks.testing` and a key unique within your mod.
Names must use letters, digits, underscores, dots or hyphens (no spaces).
Contributions are ordered by order, mod ID and key; the backend scopes widget IDs
so two mods can use identical button labels safely. Resource cleanup removes only
the unloading mod's contribution. `panel` remains available for independent windows.

The standard shared windows in this release are `bbmod.dashboard` for status and
`bbmod.settings` for controls. Inspector and Adaptive Triggers use both; the
[UI example](../examples/ui-examples/README.md)
demonstrates identical labels, versioned state transfer and dynamic removal:

```rust
let contribution = cx.contribute("bbmod.dashboard", "my-status", 100, |ui| {
    ui.text("My mod is loaded");
})?;
// Later, from the same loaded mod instance:
cx.release(contribution)?;
```

Store resource handles only in the live instance. Save logical UI values for
reload, then register fresh contributions. Removing the example's settings section
does not remove the slot or the other mods' sections. If a mod retains compatibility
with older UI backends, fall back to `panel` specifically on `Error::UNSUPPORTED`;
do not disguise invalid keys or duplicate registration as a missing capability.
Use `on_frame_mut` when a queued action needs to register a new Rust closure, such
as recreating a contribution. Its mutable `Cx` is valid only in the serialized
host callback; do not store it, call it from a worker, or re-enter that mod's callbacks.
Ordinary `on_frame` continues to provide immutable SDK access for reading and requests.
Releasing a registration stops callback delivery immediately; the Rust SDK retains
the closure allocation until the mod is destroyed, as it does for existing panels
and frame callbacks. The example's finite remove/re-add exercise is suitable for
testing ownership. Avoid unbounded re-registration loops in long-lived production
mods; ordinary show/hide state can remain inside a single registered closure.

This is the host's ImGui overlay. Creating, changing or intercepting the game's
native widgets and menu events remains research. Do not advertise overlay slots as
native menu integration. Consult the equipment/UI investigation report for current
evidence and gaps. The current equipment snapshot exposes raw slots and selectors;
named active weapon semantics and complete unique attack/hit attribution remain
unverified, even with the limited native behavior observations.

## Carried inventory

`bbmod::inventory` provides occupied entries in carried inventory, excluding storage.
The shared host cache refreshes at most every 250 ms when queried or subscribed;
mods do not each run separate heap scans. A page contains up to 64 entries and the
whole snapshot is bounded to 4096 slots. Start `inventory_page(0, 0, 64)` with the
latest revision, then pass that revision for subsequent pages. A changed revision
fails with `Error::STATE` rather than mixing snapshots. `inventory_snapshot()`
performs this checked pagination and returns owned entries; retry later on failure.

Entries expose slot, opaque reference, quantity, raw metadata and encoded item ID.
Use the ID only when `flags & bbmod::inventory::ID_RESOLVED != 0`; unresolved entries
remain present. Do not infer a weapon name or item category from an opaque handle.
Inventory changes can coalesce; a smaller quantity is not proof that an item was
used. The native item-use result is a separate event. Enumeration does not authorize
raw inventory writes or replace native award/consume ownership semantics.
