# People behind doors and windows

[Guide index](README.md) · [Investigation method](RESEARCH.md)

Related: [NPC quest states](NPC-QUEST-STATES.md), [traversal doors and shortcuts](DOORS-AND-SHORTCUTS.md)
and [persistence](PERSISTENCE-AND-RESETS.md).

## The pattern

Bloodborne lets the player speak to people through closed architecture. Gilbert is
encountered at a window near the Central Yharnam lamp; Iosefka initially speaks from
behind the clinic door. Red lights mark many early residential conversation points.
See the [Central Yharnam walkthrough](https://gamefaqs.gamespot.com/ps4/181175-bloodborne-complete-edition/faqs/80064/central-yharnam)
and [clinic encounter description](https://primagames.com/news/bloodborne-guide-central-yharnam-cleric-beast-boss-fight-all-collectibles).

The player's useful action here is to address the occupant. Entry into the building
is a separate question. Record the actual prompt rather than assuming a door-shaped
object offers an opening action.

## Design interpretation

The closed boundary creates a social encounter within a hostile street. Voice and
architecture establish an occupant without requiring the player to enter a room or
see the speaker. Keeping the resident out of view also leaves room for uncertainty
about their condition and intentions.

The recurring light is a learnable cue. It suggests a place worth approaching, but
does not establish whether a particular response is currently available or whether
the occupant has a substantial quest. Decorative doors and traversal doors still
need to be distinguished by their actual behavior.

## A conversation has a lifecycle

Gilbert is a useful example because his interaction changes with progression. His
reference page also records a response to repeatedly attacking the wall near his
window, after which ordinary interaction stops. This makes both progression and
player interference relevant investigation cases.
[Gilbert reference](https://www.bloodborne-wiki.com/2015/03/gilbert.html).

Use the following as a checklist to investigate, not a universal state machine:

| Case | What to record |
| --- | --- |
| First approach | Audible cue, visible cue, prompt and first response |
| Immediate repeat | New dialogue, repetition, menu or no response |
| Return after progress | Changed response, availability or occupant |
| Offer or reward | Conditions, inventory change and behavior on repetition |
| Interrupted conversation | What happens after moving away or another interruption |
| Unavailable encounter | Whether the cue remains and what feedback the player receives |
| Travel or reload | Which results persist and which presentation state resets |

Avoid treating silence as proof that the actor or logic has been removed. It is an
observation with several possible explanations until the relevant state is traced.

## How to investigate the implementation

Start with the exact window or door and a known game state. Trace the interaction's
spatial association, dialogue selection, text/audio references and persistent results
separately, following the [research guide](RESEARCH.md).

Questions worth answering for a concrete example:

- What owns the interaction point: a character, an object, a region or another record?
- Which conditions enable the prompt and select a response?
- What connects the spoken line, subtitle and speaker identity?
- Does the interaction set a flag, give an item or affect another encounter?
- What reconstructs the result when the map loads again?

**Unresolved here:** exact entity IDs, dialogue state IDs, event conditions and hidden
actor arrangements. The visible absence of a speaker does not prove the absence of
a character entity. No local record has yet been mapped to Gilbert or the clinic
encounter for this guide.

## Reusable lesson

For an original mod, a boundary conversation can make scenery socially meaningful.
Specify the cue, response selection and persistence rules explicitly. If an occupant
becomes unavailable, decide what the player should hear or see on returning.
