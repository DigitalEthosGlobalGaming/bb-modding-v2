# Common component catalogue

Status: proposed target components. Only the baseline column describes existing
SDK behavior. A logical component does not imply that the native game has an
identical general-purpose widget.

## Foundation and layout

| Component | Responsibility and composition | Baseline / work required |
| --- | --- | --- |
| Screen | Full-screen, menu or modal root; title, content tree, navigation policy | Logical menu Screen exists; full-screen composition requires a renderer/binding contract |
| HudSurface / Anchor | Persistent passive root, safe-area placement, shared regions and visibility policy | New native HUD mounting/context contracts |
| Row / Column | Horizontal/vertical stacking with spacing and alignment | New layout model |
| Panel / Group | Framing, heading, help and shared state for children | New composition contract |
| SplitPane | List/detail or multi-column region allocation | New layout and focus-region integration |
| Grid | Repeated cells and explicit directional neighbors | New layout; bounded and virtualized variants |
| Overlay / ModalLayer | Ordered layers, clipping and modal input ownership | New shared layering service; no implicit input capture |
| ScrollRegion | Clipped viewport, scroll position, scrollbar and focus reveal | Research stock scrolling; one owner per scroll axis |
| Spacer / Divider | Layout space or visual separation without focus | New layout primitives |

## Display and input controls

| Component | Required behavior | Baseline / work required |
| --- | --- | --- |
| Label / RichText | Text roles, wrapping, localization and overflow; supported spans only | Info rows exist; general text layout is new |
| Icon / Image | Declared resource handle, aspect ratio, placeholder, optional accessible label | New resource and lifetime contract |
| Action | Label, help, enabled/pending/result state; semantic activation | Action rows exist |
| Toggle | Accepted boolean value, reason when unavailable, change feedback | Toggle rows exist |
| Choice | Named alternatives, selected value and explicit commit/cancel policy | Rust child-screen helper exists; native inline/pick-list bindings need research |
| Slider / Stepper | Bounds, step, fine/coarse adjustment, units, accepted value | Integer helper exists; native slider binding is new |
| TextField / SearchField | Edit lifecycle, Unicode, byte limits, submit/cancel/busy states | Shared system dialog exists; controller-only entry unavailable |
| Progress / Status | Indeterminate/determinate work, stale/error/success state | Can approximate with Info; native visual component is new |
| KeyGuide / ActionBar | Contextual actions and current-device prompts | New shared service; prompts follow semantic bindings |

## Navigation and data components

| Component | Required behavior | Dependencies |
| --- | --- | --- |
| TabBar / TabView | L1/R1 switching, visible active title, per-tab state, disabled tabs and optional wrap | Screen, navigation scopes, content mounts |
| VirtualList | Stable keys, bounded visible rows, focus reveal, empty/loading/error states | ScrollRegion, revisioned data provider |
| Table | Column definitions, alignment, sort, row selection and optional row actions | VirtualList; horizontal focus policy |
| FilterBar | Category/choice filters, clear state and result count | Choice, list query state |
| DetailsPanel | Structured fields, description, icon and conditional actions for selected key | Text/Image, domain adapter |
| ContextMenu | Focused item's actions, disabled reasons and Back to opener | ModalLayer, Action, stale-item checks |
| ConfirmationDialog | Explicit action/cancel labels and default safe focus | Modal ownership, typed result |
| QuantityDialog | Bounded numeric choice, total/summary and explicit confirmation | Stepper/Slider, confirmation lifecycle |
| Toast / Notice | Brief status without stealing focus; actionable errors remain inspectable | Host-owned lifetime and layering |

## HUD components

All are proposed and reuse general layout/display primitives. Native mounting
must be researched separately from the existing stock HUD layout patcher.

| Component | Required behavior | Dependencies |
| --- | --- | --- |
| ResourceBar | Label/value/range, clamping, fill direction and optional delayed change | HUD mount, text and bar rendering; explicit data source |
| StatusEffectRow | Stable effect IDs, stacked icons, duration/count and expiry | Row/Grid, Icon, bounded live collection |
| CooldownIndicator | Remaining/ready state, declared clock and unavailable data | Progress, Icon/Text; optional radial fill capability |
| ObjectiveTracker | Objective groups, progress and bounded expanded content | Column, Label, Progress |
| QuickSlot / AbilitySlot | Icon, count, availability and device-correct prompt | Icon, Status, KeyGuide; display alone grants no action capture |
| HUD layout editor | Move/scale/reset owned groups, apply/cancel and safe-area preview | Explicit interactive mode, Screen/Modal arbitration, settings persistence |

## Composed interfaces

An InventoryBrowser is a Screen + TabView + FilterBar + VirtualList/Table +
DetailsPanel + ActionBar, optionally with QuantityDialog and ConfirmationDialog.
A SettingsPage uses Group + Toggle/Choice/Slider/TextField + apply policy.
A CharacterBrowser reuses the same list/detail composition with a different
data provider. Avoid a bespoke inventory-only UI architecture.

Every interactive component needs a contract for normal/focused/disabled/hidden,
pending, failure and empty states where applicable. Help must remain readable
without activating a disabled action. Accepted value and selection are distinct.
Long labels, localization, contrast, font sizing and target-resolution layout
are part of component acceptance, not a final cosmetic pass.

## Native research candidates

| Game movie family | Questions to answer before choosing a binding |
| --- | --- |
| `optionsetting`, `slider`, `chrmake_multislider` | Which native builders bind value, bounds, title, help and focus? Which layouts have fixed slots? |
| `numselect`, `largenumselect`, `itemquantityselect` | How do adjustment, commit/cancel, limits and modal ownership work? |
| `commandlist`, `talkchoice1/2/3` | How are actions, disabled states and Back delivered? |
| `inventory`, `itemlist_*`, `itemstatus_*`, `itemdetailtext` | How are categories, scrolling, selected-item details and item actions connected? Can the widgets accept generic data? |
| `ingametop`, `title`, `common_base` | What can safely host a full-screen composed root and control transitions/backgrounds? |
| `messagebox`, `ingamemessagebox`, `systemdialog` | Which dialog supports arbitrary messages/buttons and safe return? |
| `detailkeyguide`, `iconhelp` | How are dynamic prompts and help bound? |
| `fe`, `remotepcfe`, `caption`, `summonmessage` | How can owned HUD groups mount/update safely, layer with the stock HUD and observe visibility contexts? |

Names identify candidate assets, not proven behaviors. Inspect nested placements,
frame labels, visibility/opacity/masks, script exports, external textures/fonts,
native factories, providers and action routing. Prefer a small set of verified
layout templates over an unsupported promise of arbitrary native layout.
