> Historical workspace notes. Paths and validation describe the original workspace; use ../SETUP.md for this toolkit. Inspector and bulk recovered tables are excluded.

# Enemy, boss and AI development

ABI 1.6 adds a shared **read-only sampled character registry**. This is the first
enemy/boss hook layer, not an AI replacement or native damage interceptor.

## Implemented

`Cx::characters()` returns an owned, revision-consistent list of loaded characters.
`characters::CHANGED` announces a cache revision through the existing shared event
bus. `Event::characters_changed()` decodes its summary. `CharacterTracker` turns
successive snapshots into `Reset`, `Appeared`, `Disappeared`, and `Changed`
observations. Subscribers install no native detours and never receive mutable
guest pointers. Unload/reload uses the existing host-owned event lifecycle.

Each entry has a host token, native handle, character class, raw team value,
optional current/max HP, optional NPC parameter/name IDs, AI module presence,
optional raw script-event value, backread state and update interval. Check flags
before reading optional fields: `HEALTH`, `NPC_PARAM`, `SCRIPT_VALUE` and
`AI_MODULE`. AI presence is not proof of active thinking or aggro. Script values
are raw event-module state, not named AI goals or boss phases.

```rust,ignore
// Mod::load
cx.subscribe(bbmod::characters::CHANGED)?;

// Mod::event; keep a CharacterTracker in the mod's state.
if let Some(summary) = event.characters_changed() {
    if summary.flags == 0 {
        self.tracker.reset();
    } else {
        match cx.characters() {
            Ok(snapshot) => for change in self.tracker.update(snapshot) {
                if let bbmod::characters::Observation::Changed { before, after } = change {
                    if before.flags & bbmod::characters::HEALTH != 0
                        && after.flags & bbmod::characters::HEALTH != 0
                        && before.health != after.health {
                        cx.log_info(&format!("{}: HP {} -> {}", after.token, before.health, after.health));
                    }
                }
            },
            Err(_) => self.tracker.reset(),
        }
    }
}
```

Also request an initial snapshot after loading: an unchanged global cache need
not emit another event just because a new subscriber joined. The example mod
does this from its first frame callback.

`boss_archetype(&character)` labels 19 known NPC name IDs from the reference boss
catalog. This is incomplete and does not identify a unique encounter: variants,
doubles, phases and chalice characters can share labels. NPC class includes
friendly NPCs. Player-class NPCs are not classified by this label helper.

## Sampling and identity limits

The host samples at most once per 250 ms, only when queried or subscribed. There
is no heap search. It reads the fixed WorldChrMan pointer and bounded ChrSet
tables: at most 62 sets, 4096 slots per set, 8192 total slots and 512 occupied
characters. Exceeding a bound returns a budget error rather than a truncated list.
Pages contain at most 32 entries and reject changed revisions.

Pointer chains, handle indices, entry backlinks and module owners are checked;
set headers, slots and world pointers are rechecked. Reads are fault-safe but do
not freeze the game or make the snapshot atomic across native workers. A changing
or unsupported layout makes the sample unavailable. Unavailable samples clear
the cache; recovery, world/player changes and sampling gaps over two seconds
start a new epoch. These boundaries reset comparisons rather than invent deaths.

Tokens distinguish observed actor/data/handle lifetimes. They are not native
generation counters. Free/reallocate at the same addresses and same handle
entirely between samples can remain undetected. Never use these tokens as write
authorization. HP changes are observations, not confirmed hits or attacker
attribution; zero HP is not a defeated-boss flag. Disappearance can be backread
unloading, map transitions or retirement. Fast changes can be missed/coalesced.

## Evidence

Local reference: [bbhost](https://github.com/droogie/bbhost), commit
`566184a74f071166299bb656c4af479e5bbe2771`. Relevant primary source files:
`include/bbhost/engine/sprj/world_chr_man.hpp`, `chr_set.hpp`, `chr_handle.hpp`,
`chr_ins.hpp`, `chr_module.hpp`, `target.hpp`, `world_ai.hpp`, `params.hpp`, and
`plugins/boss_rush/boss_rush.cpp`. Layout facts informed an independent reader;
reference implementation code was not copied into the Rust SDK.

Disassembly of the private 1.09 ELF confirms the WorldChrMan singleton, handle
selector/index extraction, set table and slot stride at RVA 0x12a00b0; NPC vtable
installation at 0x18e48c9; NPC parameter reference at 0x18e4a36; and the event
module value writer at 0x1a59190. Ten exact instruction checks gate the service.
See `../discoveries/character-profile.json` and its read-only verification script.
Fixed instruction checks are short direct reads, not startup scans.

The SDK's existing NpcThinkParam definition is 148 bytes and matches the local
dump. Its field names/layout come from the existing Openborne data integration.
This does not establish the runtime unit/behavior of every field.

## Useful next hooks, in practical order

1. **AI parameter overlays:** merge edits to `NpcThinkParam` for `eye_dist`,
   `ear_dist`, `BattleStartDist`, `nearDist/midDist/farDist`, `maxBackhomeDist`,
   `SightTargetForgetTime`, `logicId` and `battleGoalID`. Inspector already knows
   this table; the item content compiler currently does not edit it. Extend the
   compiler with field validation and deterministic per-field conflict handling.
   Changing a row may affect several enemies that share it.
2. **Boss encounters and phases:** correlate character observations with map
   event flags and the native boss health-bar lifecycle. Multi-actor fights,
   transformations, first-entry scenes and victory flags need separate treatment.
   An HP threshold mod is feasible now; a threshold is not an authoritative phase.
3. **Target changes:** `SprjChrInsHandleTargetAccessor` has a handle and cached
   character-set entry; binder RVA 0x12a00b0 is identified. Trace which owner uses
   each accessor before calling it an enemy's current target. Position and sound
   targets are different classes, so not every target identifies a character.
4. **Stagger and hit-stop:** super-armor fields and hit-stop timing have concrete
   module layouts. These could support stagger meters or haptic cues after native
   fingerprinting and controlled tests establish transitions and ownership.
5. **AI goal/decision interception:** SprjChrAiModule is only a 0x10-byte shared
   header; no decision state is mapped there. SprjWorldAiManager owns Lua objects,
   area/block state and file handles. Its known Lua tick is a maintenance helper,
   not proven to be the complete decision loop. Trace goal dispatch and current
   target first; establish thread/return semantics before offering mutation.
6. **Damage and effects:** damage application/receive paths and effect-list APIs
   are research leads. A shared pre-commit policy needs validated attacker/victim
   identity and ordered modifier ownership. Existing HP sampling cannot substitute
   for this. See `item-gameplay-events.txt` for hit research.

Do not use the raw update interval or backread flags as an AI pause feature:
those fields affect broader actor scheduling/collision and reduced updates.

## Validation and example

`../modding/mods/character-observer` contributes **Enemy and Boss Observer** to the F1
shared dashboard. It is read-only and uses ABI 1.7's sortable, scrolling table.
Search, NPC/boss/injured filters, and 25/50/100/All pagination replace the former
40-row cutoff. The default sort places recent HP changes first; every column
can be sorted by clicking its header. Unknown values sort last. Pause display
freezes the sampled view. Eight recent observation lines remain in a separate
section; messages identify changed fields rather than repeating unchanged HP.
Logging is opt-in and capped per refresh.

Offline native tests cover bounds, handle/owner checks, optional fields, two
subscribers, shared pagination, profile mismatch, HP zero, unavailable recovery,
and idle cost. The real DLL harness loads two copies, checks separate UI scopes,
reloads one ten times and unloads each independently. Debug and assertion-enabled
optimized tests pass. Four Rust tests cover payload/layout, older hosts, token
replacement, epoch resets and boss label guards. No game was launched for this
work; real enemy/boss coverage and performance remain to be tested.

The release is installed with a rollback backup. See
`../CHARACTER-RELEASE-VALIDATION.txt` for exact results and the manual checklist,
including the existing OS-suspension stress test's initial watchdog timeout and
successful isolated rerun. No native hook engine behavior was changed.
