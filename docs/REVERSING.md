# Finding things in Bloodborne with the Inspector

A method for going from "I don't know where anything is" to "I have the player's
health address and the function that changes it". The **Inspector** mod
(`examples/inspector`) is the tool; this is how to use it. Open bbport's menu
(Insert / L3+R3) and expand **Inspector**.

Before anything else: **nobody has handed us verified Bloodborne 1.09 struct
offsets, so don't trust any number you didn't confirm yourself.** The one map we
*do* trust is bbport's `patches/Bloodborne.xml` — every address in it is a real
1.09 location that works through `cx.ps4(..)`. Start from there and from what you
discover live.

## The tools

| Section | What it does | Use it for |
|---|---|---|
| **Params** | Finds Bloodborne's game data tables in memory (items, weapons, armor, enemies `NpcParam`, shops, item lots, character init, ... 63 known), decodes rows field by field, labels items/weapons/armor with English names, and edits fields live. Edits are owned patches: they revert when the mod unloads. | Changing what things *are*: vial cap, weapon damage, enemy HP/souls, shop stock. |
| **Live Memory Scan** | Cheat Engine style scan of the game's heap: first scan for a value, change it in game, refine (= value / changed / unchanged / increased / decreased). Runs on a worker thread. | Finding live state: your HP, your vial count, an enemy's current HP. |
| **Call Logger** | Hooks a function start; shows its last calls' six integer arguments. arg0 (rdi) is usually `this`. | Grabbing object pointers from a function you know. |
| **Register Watch** | Hooks *any* instruction and shows all 16 registers there (red zone and flags preserved). | The instruction that writes a field: base register = the object, displacement = the field offset. |
| **Struct View** | Memory grid, changed bytes in `[brackets]`, follow pointers, Back. | Reading an object once you have its address. |
| **Value Finder** | Searches the eboot image for a constant or an address. | Static anchors (globals that point at live objects). |

How the Params section knows it found a real table (nothing is a hard-coded
address): it searches for the table's type name (e.g. `EQUIP_PARAM_GOODS_ST`),
then requires the 64-bit param structure Openborne's dumps confirm — first row's
data at `0x40 + rows × 24`, increasing ids, a sane stride — and uses a field
layout only when its size equals the stride it measured in memory.

## Opening it, and keeping a record

Press **F1** in game; the Inspector is one of the panels. Everything it finds can go to
`bbmods.log` (beside the `bbmods` folder) and the in-game Mod log window:

* always: located param tables, every param edit (`param edit: EquipParamGoods row 1000
  maxNum: 20 -> 99`), every scan's result with its first 50 addresses;
* when you tick **log ... to file** under a hook: each call's arguments / each watch hit's
  registers (at most 10 lines per second per hook; the rest are counted);
* on request: **Dump to log** (Struct View: hex plus i32/f32/pointer notes per 8 bytes),
  **Dump row to log** and **Dump table to log** (Params: every field, named).

The log is the easiest thing to share when you want help reading what you found.

## Editing params

Pick a table and a row; each field has a box with its current value. Type a new value and
press **Set** (the `x` discards the typing). A value that does not fit the field's type is
refused. Edited fields are marked `*`, listed under the table, and all revert when the mod
unloads or with **Revert all edits**.

## First wins (no addresses needed)

**Items: raise the Blood Vial cap.** Params → Locate params (in the game world,
not the title screen) → table `EquipParamGoods` → row `1000 Blood Vial` →
type a new value in the `maxNum` box → Set. Unload the mod and it reverts.

**Enemies: change an enemy's stats.** Params → `NpcParam` → pick a row → fields
such as `hp` and `getSoul`. Rows are templates, so a change shows on enemies
spawned after it (rest at a lamp or reload the area).

**Your live HP / vial count.** Live Memory Scan → type `i32` (try `u16` for item
counts) → value = what the game shows → First scan. Use a vial / take a hit →
new value → `= value`. Repeat until a handful remain; Inspect opens the struct
around it. Neighbouring fields of the same object are usually right there.

## From a live value to the code (the next layer)

1. Live scan finds the address of the value (say your HP).
2. To find the **instruction that writes it**, you need its code address. Load
   `eboot.bin` in Ghidra/IDA at base 0x400000 and look for stores with the
   field's offset (Struct View tells you the offset from the object start), or
   find the code via a param (damage reads `AtkParam`/`NpcParam` fields).
3. **Register Watch** that instruction: the base register is the object
   (player or enemy), and every hit shows which one. Inspect it.
4. Now hook the function containing it with `bbmod::detour!` to change the
   behaviour (scale damage, block death), or write the field each frame.

(Hardware watchpoints — "break when this address is written" without knowing
the code — are the planned next tool; see ROADMAP.)

## Reading addresses

Everything is shown as a **PS4 address** (eboot at 0x400000, the same as
Ghidra/IDA with the eboot loaded there, and the same as `Bloodborne.xml`). Type
a PS4 address plainly (`0x04d99138`) or a host address with an `h` prefix. In a
mod, `cx.ps4(0x04d99138)` turns the first into a usable pointer.

## Playbook 1 — the player and its stats (HP, stamina)

The goal is the player character object and the offsets of its fields.

1. **Get a `this` pointer.** You need a function that runs often while you play
   and takes the player as `this`. Two ways to find one:
   * In Ghidra/IDA on `eboot.bin` (base 0x400000), find an update/tick function on
     the player (search strings, or start from a known address in `Bloodborne.xml`
     — the camera and "no dead" entries touch player-adjacent code).
   * Or hook a few candidate addresses and watch which fire every frame with a
     stable pointer in rdi. A per-frame method's rdi is the same object each call.
2. **Log it.** Call Logger → paste the address → **Log calls**. Let it run a
   second. Note `rdi` (that's `this`).
3. **Inspect it.** Click **Inspect arg0**. Struct View opens on the object.
4. **Make a field move.** Take a hit (HP), sprint (stamina). The bytes that change
   highlight. HP/stamina are usually two `f32` or `i32` near each other (current
   and max). Confirm: heal, the value rises; the "max" one stays put.
5. **Write it back in a mod.** The offset from `this` is stable within a version:
   ```rust
   // this_ptr came from a hook; hp_off you found in Struct View
   let cur: f32 = cx.read_val(this_ptr + hp_off)?;
   cx.write_val(this_ptr + hp_off, cur.max(9999.0))?;  // not reverted (game data)
   ```
   To reach the player from a mod without a live hook, you need a **static anchor**
   (next playbook) so you can resolve `this` yourself each frame.

## Playbook 2 — a static anchor (reach an object every frame)

A hooked `this` only exists while the hook fires. To read the player on your own
schedule, find a global that (through a short pointer chain) reaches it. FromSoft
engines keep a handful of world/game-data singletons for exactly this.

1. With a live `this` from Playbook 1, copy its host address.
2. **Value Finder** → type that address → type **address** → **Find**. Matches are
   places in the eboot that hold a pointer to your object (or near it). A match in
   static data is a candidate anchor.
3. Open a match in Struct View and follow it: does `[[anchor] + a] + b ...` land
   back on the object? When it does, you have a chain.
4. In a mod, resolve it live:
   ```rust
   let player = cx.follow(cx.ps4(ANCHOR), &[A, B])?;  // [[ANCHOR]+A]+B
   let hp: f32 = cx.read_val(player + HP_OFF)?;
   ```
   `follow` reads a pointer at each step and errors on a null link, so it is safe
   to call every frame even before the player exists.

## Playbook 3 — enemies (the character list)

Enemies are the same object type as the player, held in a list/table off a world
singleton.

1. Find the player object (above). Enemies share its layout, so the field offsets
   you found apply to them too.
2. Find the world singleton: Value Finder for the player's host address, follow
   matches back until you reach a structure that also holds *other* character
   pointers (a count + array, or a linked list). Struct View: an array of
   pointers shows as a run of "`+xxx -> 0x..`" follow buttons.
3. Hook a per-enemy method (damage, AI tick) and log it: every call's rdi is a
   different enemy `this`. That both confirms the object type and, over a few
   calls, shows you the range of valid pointers.

## Playbook 4 — inventory and items

1. Items are usually reached from a **game-data** singleton (separate from the
   world/character one), via an inventory container on the player or save data.
2. Find a function that runs when you use or pick up an item (Call Logger on
   candidates; the one that fires exactly when you use a Blood Vial is it). Its
   arguments point at the item and/or the inventory.
3. Inspect the item object: item id, quantity and durability are small integers
   that change as you use/drop items — Struct View highlighting finds them.
4. The inventory is typically an array of `{item id, count, ...}` records; in
   Struct View it looks like a repeating stride. Once you know the stride and the
   count/id offsets you can read or edit the whole inventory.

## Turning findings into a mod

* **A value that should never run out** (HP, Blood Vials): read-modify-write each
  frame with `cx.write_val` from a `cx.on_frame`, after `cx.follow` from your
  anchor. Not reverted on unload (it is game state), which is correct.
* **Behaviour** (what a function does): hook it with `bbmod::detour!` and change
  the arguments or the return. Get the **signature** right (System V: integer/
  pointer args in order; see RUST_SDK.md). `detour!` falls back to the original on
  a panic, so a wrong guess is survivable while you iterate.
* **Bytes at a fixed address** (a flag, a constant): `cx.patch(cx.ps4(addr), &[..])`
  — owned, reverted on unload, exactly like an XML line.

## Limits and safety

* The Call Logger captures the **six integer/pointer argument registers** of the
  most recent call, plus a count. It does **not** capture the return value, float
  arguments, or 7th+ (stack) arguments, and it clobbers `rax` — so **varargs and
  float-heavy functions are poor targets**. For those, hook with `detour!` and
  read what you need explicitly.
* Hooking an address that is **not a function start** can crash the game. A crash
  in your own callback disables just the mod; a crash *inside a hook the game
  calls* ends the session (the fault names the mod). Prefer addresses from
  Ghidra/IDA or a pattern scan, and keep `BB_MODS_SAFE=1` handy.
* Offsets are stable within one game version. Pin them to a version check, or find
  them by pattern scan so they survive small shifts.
