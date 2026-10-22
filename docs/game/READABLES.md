# Readable objects and items

[Guide index](README.md) · [Investigation method](RESEARCH.md)

Related: [loot and item awards](LOOT-AND-ITEM-AWARDS.md) and
[persistence and resets](PERSISTENCE-AND-RESETS.md).

## Several ways to encounter text

“Readable” describes a player experience, not a single confirmed technical category.
Distinguish the following before looking for implementation records:

| Kind | Player action | What to investigate |
| --- | --- | --- |
| Authored world note | Inspect a particular location | Interaction point, text selection, progress conditions and repeatability |
| Inventory description | Open the details for an item | Item definition, localized description and access through the menu |
| Messenger message | Read a message at a world location | Authored tutorial versus player message, availability and online dependencies |
| Interaction feedback | Attempt an action and receive text | The condition causing feedback and whether the action changed state |

The handwritten scrawl in the clinic's operating room and the warning at Old
Yharnam's entrance are examples of authored world notes. The
[World Notes reference](https://www.bloodborne-wiki.com/p/world-notes.html) lists their
locations and presentation text. Reading such a note should not be assumed to add
an inventory item: check the outcome of that specific interaction.

## World notes: information attached to a place

Design interpretation: placing text in the world gives information a location and a
moment of discovery. The surroundings contribute meaning, and the player chooses
whether to stop and inspect. A clue can direct exploration while its author or
reliability remains uncertain.

For each note, record:

- The location and physical cue that led to the prompt.
- Whether the note is available immediately or appears after a condition.
- Whether inspecting it only displays text or also produces another result.
- Whether it can be read again, including after travel and reload.

Do not infer a “read” flag simply because text was displayed. A persistent result
needs its own evidence.

## Item descriptions: information attached to possession

Inventory descriptions let the player revisit information away from the place where
an item was found. An item can combine a mechanical purpose with narrative context.
For example, the Rune Workshop Tool enables rune use at the Memory Altar and has
an associated description.
[Rune Workshop Tool reference](https://www.bloodborne-wiki.com/2015/03/rune-workshop-tool.html).

Design interpretation: collecting an object can also collect a piece of the story.
When documenting it, separate the pickup interaction, inventory entry, detail text
and mechanical effect. A sentence describing an object's purpose does not establish
which code or data enforces that purpose.

For research, follow the item definition and its text references. For original
content work, use the [content workflow](../CONTENT.md) and write original text.

## Messenger messages: identify the source

The Notebook supports writing and reviewing player notes; the Hunter's Dream also
contains messages left by the game. A similar presentation does not establish the
same origin or availability rules.
[Notebook reference](https://www.bloodborne-wiki.com/2015/03/notebook.html).

Record whether an example is an authored tutorial message or a player message.
For player messages, record online availability separately from the offline world
note examples. An offline check cannot establish how live player messages behave.

## Research boundaries

Text content, interaction eligibility and persistence are separate findings. Finding
a matching localized string is a useful lead; it does not prove the string is used,
identify its caller or reveal the conditions under which it appears. Likewise, a
message ID does not identify a world object without a traced relationship.

**Unresolved here:** the individual note/message IDs, owning entities and event or
dialogue records. The initial examples are source-backed descriptions, with design
interpretation, rather than local gameplay tests or completed implementation traces.

## Reusable lesson

Choose where information should live. World text rewards visiting a place; inventory
text supports later review. For a mod, decide separately whether reading changes
state, whether the text remains available, and how the player can find it again.
