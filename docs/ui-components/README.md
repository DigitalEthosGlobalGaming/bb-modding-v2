# Composable native UI components

Status: design proposal, 20 October 2026. This directory plans SDK work; it does
not describe released APIs or claim new native functionality has been verified.

The goal is a flexible component system for native mod interfaces: gameplay
menus, tools, settings, controller-friendly browsers and full-screen interfaces.
Components must compose into larger interactive interfaces without every mod
owning competing engine hooks or movie replacements.

## Core requirements

- Full-screen **Screen nodes**, with nested layouts, content regions, tabs,
  dialogs and contextual actions. Opening a screen must not constrain its
  contents to the stock command-list template.
- Layout composition through rows, columns, split panes, grids and layers.
  A layer has explicit focus/input behavior; visual stacking alone grants none.
- Composable HUD surfaces with anchored widgets, live data, shared regions and
  visibility policies. Passive HUDs take no input; interactive modes use explicit
  capture through the same arbiter as screens.
- Semantic controller navigation, including L1/R1 tab switching, list navigation,
  value adjustment, Confirm, Back and context actions.
- Stable selection/focus across live updates, tab changes, filtering and reload.
- Data-driven lists and details for inventories, observers and developer tools.
- Shared ownership, capability checks and safe unload/reload across mods.
- Reuse the game's visual language and verified native controls where suitable;
  keep unsupported layouts explicit instead of pretending a list is a full UI.

## Read the plan

| Document | Purpose |
| --- | --- |
| [Architecture](architecture.md) | Screen and HUD models, composition, state, input, ownership and compatibility |
| [Component catalogue](components.md) | Common primitives and components, dependencies and interaction contracts |
| [Reference interfaces](reference-interfaces.md) | Full-screen inventory and tabbed settings specifications |
| [GFX candidate survey](gfx-survey.md) | Survey of all 128 loose game movies, component candidates, supplied-tool review and next native experiments |
| [Implementation plan](implementation-plan.md) | Research, ordered milestones and acceptance criteria |
| [UI-Examples-Mod delivery plan](ui-examples-mod-plan.md) | Investigation workflow, component gallery, reusable patterns, HUD showcase and completion gates |

## Current implementation boundary

The native registry currently supplies `Screen`, `Action`, `Link`, `Toggle` and
`Info` nodes. Rust `Choice` and `Integer` helpers compose child menus from those
nodes. `Text` uses the shared system text dialog and currently requires a keyboard
for typing. Static inspection found five named command slots in the stock Top
sprite. The adapter has been changed to five slots, with three data rows and up
to two navigation rows for longer lists. The fade-aware resize fix passed native
regressions and repeated live paging on 21 October. The previous implementation assumed six.

Current `Kind::Screen` is a logical native menu destination, **not a general
full-screen layout surface**. General containers, native sliders, tabs,
multi-pane lists, HUD mounting and configurable full-screen surfaces are proposed work.
Registration capability is distinct from native rendering capability.

Implementation has started; see the [latest agent handoff](NEXT-AGENT.md)
for the exact checkpoint and unfinished checks. On 22 October, the Rust SDK gained
renderer-independent `ui_state::ListState` for stable selection, revision checks
and visible ranges. It makes no native calls. An example test supplies mod-owned
browser data; a native browser renderer and input route are still unfinished.

Sources: [registry and Rust API](../../sdk/rust/bbmod/src/native_menu.rs),
[existing helpers](../../sdk/rust/bbmod/src/native_menu/controls.rs),
[live list adapter](../../src/host/src/native_menu_live.inc), and
[validation record](../VALIDATION.md). Earlier user trials confirmed base menus
and live Counter/Monitor updates. On 21 October 2026, the user confirmed repeated
paging keeps rows visible and bright after the adapter began waiting for the
native fade before resizing its list. Native traces corroborate twenty row-count
changes with full alpha and settled positions. The System heading remains open;
full-screen, tabs and HUD rendering have not yet been implemented or verified.

## Research inputs

- Local game movies: `dvdroot_ps4/menu/*.gfx` in the user's selected game.
  Keep recovered structures, scripts, textures and previews in ignored private
  references. Do not include game-derived exports in this public documentation.
- [JPEXS features](https://github.com/jindrapetrik/jpexs-decompiler/wiki/Features)
  and [CLI](https://github.com/jindrapetrik/jpexs-decompiler/wiki/Commandline-arguments):
  visual inspection and searchable exports.
- [bbhost option menu source](https://github.com/droogie/bbhost/blob/master/src/engine/option_menu.cpp):
  native section/row construction, choices, sliders and redraw research.
- [bbhost asset generation](https://github.com/droogie/bbhost/blob/master/src/engine/menu_assets.cpp):
  cloned section layouts, title bindings and scrollbar integration.

These upstream links were inspected on 20 October 2026 and can change. Record
commit IDs before implementation comparisons. Confirm every borrowed assumption
against the supported local game/profile; importing code requires provenance and
licence review. No external implementation proves our adapter works in-game.
