# NPC conversations and quest states

[Guide index](README.md) · [Door conversations](DOOR-CONVERSATIONS.md) · [Persistence](PERSISTENCE-AND-RESETS.md)

## The pattern

An NPC encounter can change through conversation, player choices, world progression
and events involving other characters. Explaining it as “talk until the quest advances”
misses the conditions that make a response available.

A useful descriptive model separates several questions. It is not a reconstructed
list of Bloodborne variables or a claim that quests use one common state machine.

| Concern | Question |
| --- | --- |
| Knowledge | Which destinations or topics can the player discuss? |
| Conversation progress | Which responses have already been heard or selected? |
| World progress | Which milestones permit or close an interaction? |
| Character situation | Where is the NPC, and are they available to interact? |
| Relationships | Do another NPC's presence or the player's actions affect the outcome? |
| Rewards | What has been offered, received or made unavailable? |

## Example: learning about a refuge

Speaking to the Oedon Chapel Dweller enables the player to tell refugees about the
chapel. This connects information acquired in one conversation to choices elsewhere.
[Chapel Dweller reference](https://www.bloodborne-wiki.com/2015/03/Oedon-Chapel-Dweller.html).

Design interpretation: dialogue can grant a new possibility without granting an
inventory object. When documenting a missing choice, investigate what the player
knows as well as the resident's own circumstances.

## Example: Arianna's changing circumstances

Arianna can initially be addressed at home, but her refuge request becomes available
after defeating the Witch of Hemwick, Blood-starved Beast or Vicar Amelia. The
opportunity to send her away closes at the Blood Moon. Directing her to the chapel
permits further interactions, while the clinic produces a different outcome.
[Arianna reference](https://www.bloodborne-wiki.com/2015/03/arianna-woman-of-pleasure.html).

Design interpretation: availability, branch choice and later consequences form
different parts of the encounter. Hearing an introductory line does not establish
that every later branch is available, and finding someone at a new location does
not establish how their relocation is implemented.

## Example: advice is not an order

The Narrow Minded Man, encountered across from Arianna, goes to the refuge opposite
the one the player suggests. His reference also identifies speaking to Arianna about
a safe place as a prerequisite for his departure.
[Narrow Minded Man reference](https://bloodborne.wikidot.com/narrow-minded-man).

Design interpretation: the choice records what the player says; the character's
response determines its consequence. Document the selected advice and actual
destination separately. The wording of a menu option is not sufficient evidence
for the result.

## Revisiting and exhausting dialogue

“Exhaust dialogue” is a walkthrough instruction to keep interacting until the
currently available responses are finished or repeat. It does not by itself prove
that every line sets a persistent flag, or that repeating a line advances anything.
Record each response and the condition that changed between visits.

Keep the triggering action separate from when the result becomes visible. A choice,
leaving the area and finding the NPC elsewhere are three observations. A reload may
reconstruct a result already decided, or participate in its transition; static
inspection and controlled observation are needed to distinguish them.

## Investigating the implementation

Start with a single branch and known preconditions. Trace its dialogue conditions,
selected option, consequences and map-load behavior. Then examine competing branches:
what takes priority if the NPC is unavailable, the milestone has passed or the reward
has already been received? See the [research method](RESEARCH.md).

**Unresolved here:** dialogue states, event flags, relocation entities, condition
priority and save timing. These examples are community-documented behavior, not
local gameplay validation or completed implementation traces.

## Reusable lesson

For an original mod, specify a small table of conditions and outcomes before writing
dialogue. Include repeat visits, missed opportunities and each destination's result.
Let the character's behavior explain a branch, while keeping the mechanical conditions
consistent across reloads and rewards.
