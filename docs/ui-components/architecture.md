# Component architecture proposal

Status: proposed. Names and examples here are conceptual, not callable SDK APIs.

## Screen nodes and presentation

A Screen is the root of a composed UI tree, a navigation destination and an owned
lifetime. It declares a title, presentation, content root, initial focus policy,
Back policy and input context. A Screen may contain containers and controls;
control navigation does not implicitly create another Screen.

| Presentation | Contract |
| --- | --- |
| Full-screen | Occupies the UI viewport and owns its active navigation context; supports header, tabs, body panes, footer and layered modals |
| Menu | Fits a supported native menu template; constrained layout is declared by its renderer |
| Modal | Opens above an existing screen, takes exclusive focus and returns to its opener |

Full-screen is a layout/presentation choice. It does not imply pausing simulation,
blocking online activity or changing world resolution. Pause behavior is a
separate capability and policy; unavailable pause must be reported honestly.
Full-screen is not merely a list with a large background.

The logical screen ID is independent of its engine movie/resource name. Several
screens may share one verified template; arbitrary IDs must never become asset
paths. The adapter owns native factories, bindings and template compatibility.

Full-screen layout uses a declared logical coordinate space fitted to the actual
UI viewport, with safe-area insets, min/max dimensions and overflow rules. Content
panes can grow; text and icons preserve intended proportions. Background coverage
and content width are independent so ultrawide layouts can remain readable.

## HUD surfaces and gameplay presentation

A HUD is a persistent, owned presentation root alongside Screen. It reuses the
same containers, display components and snapshot publication model, but does not
enter the screen navigation stack or acquire focus by being visible. A mod can
own several HUD groups independently of its settings/full-screen UI. HUD mounting
and its capabilities are proposed additions, not existing `Kind` values.

```text
HUD surface (viewport, passive input)
  Anchor: top-left safe area
    Column
      ResourceBar
      StatusEffectRow
  Anchor: top-right safe area
    ObjectiveTracker
  Anchor: bottom-right safe area
    Row
      AbilitySlot
      CooldownIndicator
```

Each mount declares an anchor/region, alignment, offset, size constraints,
visibility policy and host-managed layer priority. Anchors use the UI viewport
and safe area rather than world-render resolution. Support configurable scale,
opacity and offsets, long/localized text and ultrawide positioning. World-space
markers need separate projection, clipping and occlusion capabilities; they are
not implied by a screen-space HUD.

The host arbitrates shared HUD regions and deterministic ordering across mods.
Contributions in one region can stack with declared spacing. Reserve stock HUD
space where verified bounds are available; otherwise expose placement controls
and report possible overlap. Automatic avoidance of unknown third-party widgets
is not promised.

Visibility policies cover gameplay, menus, dialogs, cutscenes, loading and
HUD-hidden states only where context observations are supported. Unknown state
uses a documented fallback. Normal HUD content renders below foreground screens
and modals; exceptional notices need an explicit host-approved layer. Hidden HUDs
cannot remain interactive. Pause behavior is independent of visibility.

HUDs are input-transparent by default. A bar, objective or shortcut prompt must
not consume gameplay controls. Interactive HUD modes, such as a radial selector
or layout editor, explicitly request capture through the same foreground arbiter
as Screens. Declare consumed actions, exclusive/pass-through rules, entry/exit
and Back behavior. Reject conflicting capture. Opening a modal/full-screen UI
suspends conflicting HUD interaction. Close, focus loss, disconnect and unload
release capture and reset held input before returning to gameplay.

Live values arrive as copied, revisioned snapshots from mod-owned or supported
shared providers. Stale/unavailable data has an explicit placeholder/hidden state.
Dirty updates and bounded refresh rates avoid rebuilding every frame; animations
interpolate accepted snapshots on the UI thread with a declared clock. Cooldowns
declare pause/time-scale behavior and invalid-duration handling. Rendering and
animation completion do not establish gameplay events or authorize operations.
Bound transient notices, animations and pending updates.

Resource bars, status rows, cooldowns, objectives, quick slots and counters reuse
general components; they do not require a player service. Stock HUD suppression
or replacement is a separate optional capability with restoration on release.
Adding a custom HUD does not implicitly remove the original one.

Unmount releases only owned resources, views, animations and input leases. Use
the same generation/DLL-lifetime rules as Screens. Persist user layout optionally;
re-read live values and capabilities after reload instead of restoring pointers.

## Composition rules

```text
Screen (full-screen)
  Column
    Header
    TabBar
    SplitPane
      Column
        FilterBar
        VirtualList
      DetailsPanel
    ActionBar
  ModalLayer
    QuantityDialog (when open)
```

Each component has stable identity, properties, owned children and explicit
state/events. Components can be reused under different parents. Row/Column
arrange children; SplitPane allocates regions; Overlay/Layer controls visual
stacking. Navigation scopes declare how those regions connect.

The first layout contract should support intrinsic and fixed sizes, bounded
flex allocation, spacing, padding, alignment, clipping and explicit scroll
ownership. Reject invalid trees, duplicate IDs, cycles, excessive depth and
capacity overflow before publication. Unsupported layout must return a useful
error identifying the requirement and component.

Publish transactions atomically: validate a complete subtree update before it
becomes visible. A failed update retains the last accepted tree. Do not expose
half-registered tabs, a dangling focused control or partial layout rebuilds.

## State and event flow

Mods own domain data; components own temporary interaction state. The host owns
copied presentation snapshots, routing and native view lifetime. Changes produce
typed intents such as Activate, ValueProposed, TabChanged, SelectionChanged,
Submit and Cancel. Accepted values and failures must become visible.

Requests carry owner, component, generation and relevant data revision. Validate
again before executing a queued action. Never call retained DLL callbacks from
native widgets. Apply native mutations on the verified UI thread and route mod
logic through the host's supported serialized sync point. Domain operations may
require a different native safe point; publication is not permission to write.

List selection uses stable domain keys, not visible row indexes. Preserve the key
and scroll anchor on updates; if removed, select a valid neighbor or an explicit
empty state. Cache selection/scroll per tab. Virtualized rows must not retain
actions for a previous item after recycling. Large lists use revision-consistent
pages and bounded caches rather than materializing every entry as a UI node.

## Controller navigation and input

Use semantic actions: Navigate, Confirm, Back, Adjust, PreviousTab, NextTab,
Page, ContextAction and FocusRegion. Default PreviousTab/NextTab map to L1/R1
(LB/RB), with remapping and matching button prompts.

- The active modal receives input first. A quantity dialog blocks underlying
  tabs. A nested TabBar takes shoulder actions only when its navigation scope
  owns them; otherwise they bubble to the containing scope. One press triggers
  at most one action, including the game's original input path.
- Tabs activate on shoulder press, skipping disabled/hidden tabs; wrapping is an
  explicit property. If no tab is enabled, show a noninteractive empty state.
  Active-tab removal chooses the nearest valid tab and restores valid focus.
- Lists navigate vertically; left/right adjustment belongs to the focused value
  control. Cross-pane navigation uses explicit neighbor relationships or a
  declared FocusRegion binding; do not infer ambiguous transitions by geometry.
- Navigation and adjustment can repeat with bounded delay/rate. Confirm and tab
  changes default to edge-triggered actions. Opposing directions cancel.
- Back closes the top dialog, then returns through the navigation stack. Pending
  edits declare apply/discard/confirmation behavior. Restore the opener's focus.
- Focus is separate from hover. Hidden and disabled controls cannot activate;
  selected read-only data can still expose details/help under explicit rules.
- Release capture and reset held/repeat state on close, owner unload, disconnect
  and application focus loss. A held button must not activate the next screen.

Controller-friendly does not promise controller text typing. Search needs an
explicit text-entry capability; provide controller-operable category/filter/sort
flows without requiring text input. An on-screen keyboard is separate research.

## Ownership, shared surfaces and ABI

An owning mod can mount multiple screens, HUDs and reusable subtrees. Child lifetimes
are bounded by their parent mount. Unmount recursively invalidates actions,
cancels owned dialogs and releases input capture; it does not destroy unrelated
mods' components. An open view must not reference an unloaded DLL.

Shared extension slots declare allowed child kinds, ordering, capacity and focus
scope. Namespaced IDs and deterministic order prevent collisions. Only the host
arbitrates screen transitions and input; two mods cannot both own the foreground.
When a contributing mod unloads, select a valid remaining control or close the
empty surface and return safely. Repeated open requests must not stack duplicate
views accidentally.

Append size-gated C ABI services and stable event payloads; keep Rust wrappers
above them. Existing node values, struct layouts and calls keep their meaning.
Do not change legacy `Kind::Screen` into a different renderer silently. Explicit
presentation/subtree services can add full-screen composition while preserving
the current menu API. Exact capability names/numbers are undecided.

Query support for full-screen layout, tabs, lists, sliders, dialogs, text entry,
HUD mounting, context visibility and interactive HUD capture separately.
Unsupported composition fails before opening; a mod may opt
into a documented menu fallback. Never silently substitute an ImGui window or
flatten a complex inventory into an unrelated command list.

Screen-state persistence is versioned and opt-in. Restore stable semantic state,
then validate against current data/capabilities; never persist native pointers,
pending engine requests or focus IDs without checking that they still exist.
