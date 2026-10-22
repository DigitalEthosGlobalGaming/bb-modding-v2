# What persists and what resets

[Guide index](README.md) · [Investigation method](RESEARCH.md)

## The pattern

“The area reset” is too broad to explain an interaction. A return visit can recreate
some presentation state while preserving progression, ownership or previous choices.
Document the exact transition and the exact thing being compared.

These are analytical distinctions, not confirmed engine storage categories:

| Kind of state | Examples of what to record |
| --- | --- |
| Immediate presentation | An open menu, current spoken line or animation |
| Encounter situation | An actor's position, availability or current behavior |
| Progression result | A chosen destination, granted reward or opened route |
| Character resources | Held items, storage quantities and currency |
| Game cycle | Which journey the character is in and which rewards it offers |

## Different boundaries answer different questions

This is an investigation checklist, not a verified reset matrix. A behavior checked
under one boundary remains unknown under the others until supported by evidence.

| Boundary | What the comparison can establish |
| --- | --- |
| Interact again immediately | Repeat response, reward suppression or another available branch |
| Walk away and return | Behavior after leaving interaction range; this alone does not prove map unloading |
| Travel away and back | Reconstruction after the recorded travel route |
| Die and return | Death consequences, replenishment and encounter reconstruction |
| Quit normally and reload | Restoration from saved progress under those conditions |
| Begin another game cycle | Carryover rules and renewed or substituted sources |

Record how travel or reload was performed. Do not use a quit/reload observation to
claim the same result for death, and do not treat normal saving as a crash-recovery test.

## Example: replenishment is a transfer

Blood Vials and Quicksilver Bullets are replenished from storage after death or
returning to the Hunter's Dream. This is a resource transfer, so checking only the
held count can conceal the cost in stored supplies.
[Storage reference](https://www.bloodborne-wiki.com/2017/05/storage.html).

An illustrative calculation, assuming the held target is 20 and at least seven
vials are available in storage: 13 held plus seven transferred becomes 20 held,
while storage decreases by seven. This arithmetic explains the transfer model; it
is not a local test or a claim about all item categories and capacity modifiers.

Design interpretation: a return can restore readiness without undoing the resource
cost of an attempt. Compare both sides of the transfer before calling a resource
“restored.” See [loot and awards](LOOT-AND-ITEM-AWARDS.md).

## Example: another cycle has different carryover rules

The New Game Plus reference describes levels and much inventory carrying over,
with progression items removed and exceptions such as badges and workshop tools.
It also lists replacement rewards at the Blood Gem and Rune Workshop Tool sources.
[New Game Plus reference](https://bloodborne.wikidot.com/new-game-plus).

Design interpretation: character continuity and renewed world progression can coexist.
Another cycle is a separate boundary from loading an existing area. An item-category
label alone does not provide a complete carryover rule.

## Recording a persistence finding

Use one record per concrete example and boundary:

```text
Example / version / game cycle:
Preconditions and active mods:
Before: visible state, relevant choices, held and stored quantities:
Action and exact transition:
After: same observations measured again:
Evidence: source reference or private local observation record:
Other boundaries: unresolved unless separately checked:
```

For [doors](DOORS-AND-SHORTCUTS.md), compare access as well as appearance. For
[NPCs](NPC-QUEST-STATES.md), compare dialogue, location and prior rewards separately.
For [readables](READABLES.md), distinguish continued access to text from a persistent
consequence of reading it.

**Evidence status:** the two examples are reference-backed. No reset matrix, save
format, event-flag lifetime or gameplay behavior has been locally validated for this
page. Saved results and the logic that reconstructs them still need to be traced.

## Reusable lesson

For an original mod, write a reset policy for each piece of state. Decide what is
temporary, what survives a return and what carries into another cycle. Reconstructing
the intended state should not accidentally repeat a reward or erase a choice.
