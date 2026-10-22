# Loot and item awards

[Guide index](README.md) · [Item descriptions](READABLES.md) · [Persistence](PERSISTENCE-AND-RESETS.md)

## The pattern

An award connects a player action to a change in ownership. Describe how the item
is obtained separately from what the item does once owned.

| Source | Player-facing distinction | Questions to investigate |
| --- | --- | --- |
| Placed pickup | The player approaches a particular world location | What makes it available, and what records collection? |
| Enemy drop | An encounter produces collectible loot | Is the result guaranteed, conditional or probabilistic? |
| Conversation reward | Dialogue leads to an award | Which branch grants it, and can that branch run again? |
| Event reward | A milestone produces an award | Does it go directly to inventory or create a pickup? |
| Purchase | Currency is exchanged for an item | What controls stock, price, capacity and repeat purchases? |

This table organizes research; it does not assert that each source uses a different
underlying award system, or that any two sources share one.

## Examples: place, encounter and conversation

The [Central Yharnam reference](https://www.bloodborne-wiki.com/2015/03/central-yharnam.html)
describes a Blood Stone Shard pickup on the approach to Gilbert's Gate. The reward
is attached to a place the player explores. Blood Vials have both placed and enemy
drop sources, listed in the [Blood Vial reference](https://www.bloodborne-wiki.com/2015/03/blood-vial.html).
An item's identity alone therefore does not identify how it was awarded.

Gilbert's Flamesprayer is a conversation reward associated with returning after
entering Oedon Chapel. His reference describes a progression window for receiving
it. Document the qualifying visit and dialogue, rather than describing the award
as the inevitable result of repeating a conversation.
[Gilbert reference](https://www.bloodborne-wiki.com/2015/03/gilbert.html).

Design interpretation: placed treasure rewards attention to space; drops connect
rewards to encounters; a gift can make a relationship mechanically meaningful.
The same item can carry different significance depending on its source.

## Capacity and destination

Held quantity and stored quantity are separate. The storage reference describes
overflow being sent to storage, with a box icon indicating that destination. It
also describes Blood Vials and Quicksilver Bullets being replenished from storage
after death or returning to the Dream.
[Storage reference](https://www.bloodborne-wiki.com/2017/05/storage.html).

For an award investigation, record both quantities before and after. An unchanged
held count can conceal a successful transfer to storage. Conversely, do not assume
every item supports storage or that an award succeeds when both destinations are
full. Those cases require item-specific evidence.

## Repeatability belongs to the source

Keep these questions separate:

- Can another copy of the item exist in inventory?
- Can this particular source award it again?
- Can another source award the same item?
- Does another game cycle change the source or its contents?

A duplicate inventory entry is not proof that a pickup respawns. A missing pickup
is not proof of a successful award unless the item or another result is accounted
for. See [persistence and resets](PERSISTENCE-AND-RESETS.md).

## Investigating the implementation

Trace the source condition, award contents, destination and record of completion.
For a drop, also investigate the relationship between the actor and its possible
awards. For a conversation, trace the branch that gives the reward independently
of the displayed text. Follow the [research method](RESEARCH.md).

**Unresolved here:** award/parameter IDs, drop probabilities, full-capacity outcomes,
and ordering of delivery versus completion state. No individual award path has been
traced or gameplay-tested for this page. In particular, this guide does not establish
an engine guarantee against losing or duplicating an award during interruption.

## Reusable lesson

For an original mod, define eligibility, quantity, delivery destination and repeatability
together. Give clear feedback when delivery cannot complete, and ensure returning to
the interaction produces the intended result. Item descriptions belong to a separate
part of that design: they explain the object after acquisition.
