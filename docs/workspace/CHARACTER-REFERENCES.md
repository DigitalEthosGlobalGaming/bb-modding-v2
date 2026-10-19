> Historical workspace notes. Paths and validation describe the original workspace; use ../SETUP.md for this toolkit. Inspector and bulk recovered tables are excluded.

# Teams, scripts and enemy names

The SDK's `character_references` module contains offline dictionaries. It does
not scan memory, parse JSON or read archives when the game starts. Tables are
compiled into each mod; numeric lookups use binary search.

```rust
use bbmod::characters::CharacterExt;
use bbmod::character_references::{TeamId, ScriptValue, ActionStateId};
let enemy_team = TeamId(23).definition().unwrap();
assert_eq!(enemy_team.name, "Enemy1");
assert_eq!(TeamId(26).definition().unwrap().name, "Friendly NPC");
// A raw script value is NOT an action-state ID:
let lead = ScriptValue(9000).candidate().unwrap();
assert_eq!(lead.action_state, ActionStateId(462));
assert_eq!(lead.name, "Event9000"); // lexical research lead, not confirmed semantics
```

For a sampled `Character`, `character.team()` returns a typed `TeamId` and
`character.script()` returns `Option<ScriptValue>`, checking the SCRIPT_VALUE
validity flag. Team lookup returns None for unknown values. It describes the
team code, not whether this particular character will attack the player.
Team 23 does not distinguish bosses from ordinary enemies. No relationship
matrix or aggro-state prediction is implemented.

There are 29 documented team codes, including legacy entries. Bloodborne-specific
codes use the Bloodborne TeamType definitions from
[Soulstruct](https://github.com/Grimrukh/soulstruct/blob/12b69189a2ccebbc623a1b6565be89a18d6c9958/src/soulstruct/bloodborne/events/enums.py).
Do not substitute DS3 covenant-team labels: some external editors reuse those
labels for Bloodborne, where their meanings differ.

## Script field: unresolved semantics

The observer samples `SprjChrEventModule + 0x20`. Native set/reset functions at
RVAs 0x1a59190 / 0x1a591c0 mirror this value to actor event state and gate on
behavior-module byte +0x384; reset also requests Idle_wild. This does not yet
identify the value as an AI goal, behavior ID or current animation state.

The extracted game directory is
`game/CUSA03173/uroot/dvdroot_ps4/`. Its `action/` directory contains:

| File | Entries | Typed lookup |
| --- | ---: | --- |
| statenameid.txt | 1,012 | `action_state(ActionStateId(id))` |
| eventnameid.txt | 1,859 | `action_event(ActionEventId(id))` |
| variablenameid.txt | 124 | `action_variable(ActionVariableId(id))` |

These files use CP932, not UTF-16. The generator validates declared counts,
unique IDs, contiguous indexes, and records source hashes. `ScriptValue(9000)`
resembles the state *name* Event9000, but that state's index is **462**. The
333 `SCRIPT_CANDIDATES` are explicitly lexical-only. They are not hook targets
or proof of current state. The observer shows raw scripts as unresolved, with
candidate details under **Name / script reference notes**.

To confirm semantics next, trace the native value's writers/consumers and the
transition into named action states; validate the call chain against controlled
observations. No runtime patch should depend on the lexical candidates.

## Finding enemy names in the extracted game

`NpcParam` supplies an exact row ID, Japanese internal row name and a nameId.
English NPC-name FMG 18 provides localized text for 1,655 of 6,064 rows. Many
ordinary enemies have no localized NPC label; this is missing source text,
not a failed memory lookup. Nested archives can be read offline without editing
the original files.

All 6,064 internal Japanese row names also match the pinned Smithbox reference
data. The character generator only attaches `reference_name` when **both the
exact row ID and original Japanese text agree** (ignoring whitespace).
The SDK preserves `name` and `name_status` as official localized-text fields;
`reference_name` is separate community English metadata, sometimes a literal
or imperfect translation. It does not invent descriptions or infer identities
from numeric prefixes.

For example, row 112401 has a reference label beginning **Drowned Beast [Youth]**.
Row 117070's internal text is **屍肉烏 森**, literally a carrion crow / forest
variant; the community label is **Carrion Forest**, not an official enemy name.
The observer falls back to these labels with **[reference]**. Official names
such as Cleric Beast still take precedence. Search includes names, team labels
and raw script values; team and script columns retain numeric sorting.

For familiar canonical enemy names beyond localized FMG text, a future catalog
can join exact MSB enemy placements to models and curated model aliases. A
model label alone is not enough: variants, cut content and multi-actor bosses
must remain distinguishable.

## Files and regeneration

- SDK `data/characters.json`: exact NPC definitions, internal and reference names.
- SDK `data/character-references.json`: teams, action names and lexical candidates.
- `generate-character-catalog.py [--check]`: read-only character generator.
- `generate-character-references.py [--check]`: read-only action/team generator.
- SDK `data/reference-sources/`: pinned community inputs, URLs and hashes.

Smithbox data is pinned to commit `cbd477a8fd6d436b3e011c8548e1de8fd8876918`;
its MIT license is retained in `reference-sources/smithbox/LICENSE`.
The downloaded Smithbox team enum is retained only for research, **not used**
to generate team labels because it includes DS3-specific meanings.
