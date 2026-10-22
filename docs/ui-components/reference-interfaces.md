# Reference interfaces and interaction specifications

Status: proposed acceptance scenarios, not implemented examples.

## Full-screen inventory browser

Inspired by Skyrim's category/list/detail workflow, expressed through Bloodborne's
visual language. No Skyrim assets or code are required. The UI must support both
game inventory data and independent mod-owned inventories.

```text
Full-screen Screen: Inventory
+---------------------------------------------------------+
| Inventory                         Capacity / currency   |
| [L1] All | Weapons | Armour | Consumables | Materials [R1]|
|---------------------------------------------------------|
| Filter / sort            | Selected item details        |
|--------------------------|                              |
| > Selected item   count  | Icon / name                   |
|   Item            count  | Stats / description           |
|   Item            count  | Equipped / availability       |
|   ...                    |                              |
|--------------------------|------------------------------|
| Confirm: inspect/action   Back: return   Context: actions|
+---------------------------------------------------------+
```

The composition is Screen -> Column(Header, TabView, SplitPane(ListPane,
DetailsPanel), ActionBar), with a ModalLayer for context, quantity and confirmation.
These same components should support character/script browsers and mod tools.

Interaction contract:

1. Opening selects a valid item or shows an explicit empty state. The active
   category, selected key and scroll anchor restore when still valid.
2. L1/R1 switches enabled categories and restores each category's own selection.
   A matching prompt and visible selected tab communicate the change.
3. Up/down selects items and updates details from the same data revision. Sorting
   or filtering preserves selection by key when possible; removal selects a valid
   neighbor. Duplicate labels must not merge identities.
4. Confirm has one declared meaning for the screen (for example inspect). Context
   actions enumerate allowed operations for the selected item. Help explains
   disabled actions. Controller users can filter/sort without typing a query.
5. A multi-item operation opens QuantityDialog with current limits. A destructive
   action requires explicit confirmation when the domain requires it. While a
   modal is active, shoulders cannot change the underlying selected item/tab.
6. Submission captures item identity and revision, revalidates availability at
   execution and displays a pending/result state. Repeated Confirm cannot submit
   duplicates. A refresh is not proof that a gameplay operation succeeded.
7. Back cancels the dialog, then returns one navigation level. Close/unload
   releases capture and returns focus without passing Confirm into gameplay.

Domain boundary: the current inventory SDK exposes a bounded carried-inventory
snapshot/page API, not storage, equipment transactions, drop/sell/transfer or a
general writable inventory. Begin with read-only browsing. Enable operations
only through separately verified domain capabilities with explicit results.
Mod-owned inventories may implement their own data/actions behind the same view.
3D item previews are optional later research and must not gate the first browser.

## Tabbed settings and tools

A Screen containing TabBar([General, Controller, Gameplay, Diagnostics]) and one
mounted content region. L1/R1 switches tabs without opening new engine Steps.
Tabs may contain Groups, Sliders, Toggles, Choices, text controls or nested lists.
Screen title and active-tab title remain correct on every transition.

The screen declares immediate apply or staged edits with Apply/Cancel. Switching
tabs retains pending values; leaving with uncommitted edits follows its declared
policy. Back and Cancel do not silently commit. Unavailable tab capabilities are
explained; invalid tabs cannot trap focus.

## Full-screen tool without gameplay writes

A CharacterBrowser uses category tabs, a selectable observation list, details,
pause-observation toggle and status/help. Pausing observations does not imply
pausing the game. A mod-management screen can reuse the same layout for mod
metadata, settings and reload status. General UI components must not require
player or inventory services to be available.

## Composable HUD showcase

A passive HUD combines a resource bar, status row, ability cooldown and objective
tracker in anchored regions. Begin with explicitly labelled simulated values;
changing the demo must not adjust player HP, inventory or game time. Settings in
a full-screen Screen control group visibility, anchors, scale and opacity.

A separate opt-in layout-edit mode acquires controller capture, allows moving or
scaling the selected owned group, and provides Apply/Cancel/Reset. Opening a
foreground menu suspends conflicting editing. Verify HUD/menu/cutscene/loading
visibility only for supported observed contexts, with unknown-state behavior
shown explicitly. Close/reload/unload must leave no owned HUD or input lease.
Two owners contributing to one region must stack deterministically and clean up
independently. Optional read-only live data uses separate capability checks.

## Controller-only acceptance walkthrough

Open -> switch tabs with L1/R1 -> scroll a long list -> inspect details -> open
and cancel a modal -> change a value -> return -> reopen. Repeat with an empty
category, a removed selected item, a disabled action, a removed active tab,
controller disconnect and owner unload. Test while a button is held across a
transition. Verify focus, prompts, visible feedback and absence of leaked game
actions at every step. Text typing is a separate, explicitly supported path.
