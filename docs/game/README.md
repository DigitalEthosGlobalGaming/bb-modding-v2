# How Bloodborne works

This section documents recurring game design patterns: what the player encounters,
how an interaction behaves, and how to investigate the systems behind it. It is a
companion to the [modding guide](../MODDING.md), rather than an SDK reference or a
complete walkthrough. Examples can contain location and quest spoilers.

## Start here

| Pattern | What it explains | Examples |
| --- | --- | --- |
| [People behind doors and windows](DOOR-CONVERSATIONS.md) | A conversation anchored to scenery; dialogue that depends on progress | Gilbert's window; the clinic door |
| [Readable objects and items](READABLES.md) | World notes, inventory descriptions and Messenger messages | The clinic's handwritten scrawl; item detail screens; Notebook messages |
| [Doors, locks and shortcuts](DOORS-AND-SHORTCUTS.md) | Access requirements and connections that shorten return journeys | Gilbert's Gate; Hunter Chief Emblem |
| [Loot and item awards](LOOT-AND-ITEM-AWARDS.md) | Sources, delivery, capacity and repeatability | Placed treasure; enemy drops; Gilbert's gift |
| [NPC conversations and quest states](NPC-QUEST-STATES.md) | Knowledge, choices, progression and relocation | Chapel refuge information; Arianna; the Narrow Minded Man |
| [Persistence and resets](PERSISTENCE-AND-RESETS.md) | Comparing repeat interactions, travel, death, reload and another cycle | Storage replenishment; item carryover |
| [Investigating an interaction](RESEARCH.md) | Connecting player behavior to map, dialogue, event and text references | A repeatable investigation and evidence record |

## A useful way to describe interactions

Separate these concerns when explaining a feature. This is an analytical model for
the documentation, not a claim about Bloodborne's engine classes.

| Concern | Question to answer |
| --- | --- |
| World cue | What draws attention: light, sound, a prop, a character, a familiar visual effect? |
| Interaction point | Where must the player stand, and what action becomes available? |
| Eligibility | What makes the action available now: distance, progress, an item, a previous choice? |
| Presentation | Does the result use dialogue, a text panel, a menu, an animation or a transition? |
| Consequence | Does it give information, award something, change access or advance a quest? |
| Persistence | What survives another interaction, death, travel or a save/reload? |

A physical door and a conversation at a door can look similar while serving different
purposes. Likewise, reading text in the world and reading an item description can
communicate similar information through different player actions.

## Design principles to look for

These are interpretations of the patterns described here, rather than documented
statements of developer intent.

- **Teach through repeated cues.** A recurring cue can help players recognize another
  opportunity without a tutorial for every instance. Record exceptions too.
- **Give a place several meanings.** A familiar location can serve navigation,
  storytelling and a state-dependent interaction at once.
- **Make information part of exploration.** An interaction can reward attention with
  context or a clue, without awarding an inventory object.
- **Use revisits to reveal change.** Compare the same interaction before and after
  progress; an initial response does not describe its entire lifecycle.
- **Separate appearance from behavior.** A prop's shape alone does not identify its
  interaction logic, collision, dialogue source or persistence rules.

## Evidence and scope

The initial pages use linked community references for player-facing examples and
authored design analysis. Local inspection confirmed the presence of the archive
families listed in [the research guide](RESEARCH.md); it did not resolve individual
interactions to particular records. No gameplay validation was performed for these pages.

Keep four kinds of statements distinct when extending the guide:

| Kind | Required support |
| --- | --- |
| Referenced behavior | A source describing the behavior, with the relevant location/state |
| Locally observed behavior | A recorded game version, conditions, steps and result |
| Implementation finding | Original archive identity and the inspected relationship; IDs alone are insufficient |
| Design interpretation | Explicitly presented as analysis; do not turn it into an engine fact |

## Further topics

Add focused pages as examples are researched: lamps and travel; levers and lifts;
shops and service unlocks; death and Blood Echo recovery; combat feedback; enemy
encounters; boss encounters; world progression and its effects on familiar areas.
These are a backlog, not claims that their implementation has already been traced.
