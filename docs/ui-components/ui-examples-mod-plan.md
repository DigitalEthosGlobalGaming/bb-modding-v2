# UI-Examples-Mod: investigation and delivery plan

Status: proposed, 20 October 2026. This is a plan for SDK and demo implementation,
not an implemented mod or a claim that proposed APIs work. It extends the
[architecture](architecture.md), [catalogue](components.md) and
[implementation milestones](implementation-plan.md).

## Deliverable and scope

Deliver a buildable native Rust **UI-Examples-Mod** that demonstrates individual
components, their useful states, and reusable interactive compositions. It is
both a learning reference and the in-game acceptance application for the SDK.
The output includes authored source, a manifest, packaged DLL, usage/build guide,
component coverage index and dated verification record. The package contains no
redistributed game-derived movies, exports, textures, fonts or schemas.

Evolve `docs/examples/ui-examples` as the default home, retaining mod ID
`example.ui-examples`, crate `ui-examples` and existing reload state compatibility.
Use **UI-Examples-Mod** as the planned display name. This avoids installing a
second conflicting copy of Shared UI Examples. Review the existing UIEX state
format before extending it; use an explicit versioned migration. This document
does not rename or change the existing package yet.

All reusable controls, layout, focus/input, resources and native bindings belong
in canonical SDK/host source. The demo supplies scenarios and data, and uses only
public SDK services. No direct engine addresses, private ABI copies, per-mod
detours or GFX parsing in the demo. Helpers that several pages need should become
public SDK patterns where appropriate rather than a second UI framework.

## Investigation workflow

Investigate in small vertical slices. For each component or layout family:

1. Define its task and visible interaction contract: inputs, states, data,
   selection, update, feedback, Back and ownership. Select a stock screen that
   actually exhibits the behavior; candidate filenames are leads, not proof.
2. Inspect private GFX copies in JPEXS and export structure/scripts as supported.
   Record original SHA-256, game/version, tool/version, export command and source
   path in ignored `.local/ui-research/` manifests. Preserve symbol names and
   distinguish parsed fields from inferred meanings.
3. Trace native creation -> asset resolution -> named control/data binding ->
   input/focus -> action -> redraw -> teardown. Identify factories/providers,
   call conventions, lifetimes, shared resources and compatibility fingerprints.
4. Compare relevant upstream implementation at a recorded commit. Note licence,
   provenance, profile differences and exactly which behavior is reusable. Keep
   recovered code and game-specific address evidence private.
5. Build the smallest isolated experiment: one control/layout and deterministic
   mod-owned data. Record asset resolution and events; visually operate it in the
   game. A successful factory call or fake-engine test is insufficient.
6. Choose the supported adapter strategy and its constraints, then add the shared
   host/C ABI and Rust wrapper. Keep the existing menu API backward-compatible.
7. Add the component's showcase page, state variants and lifecycle checks. Repeat
   the real interaction loop using only the public SDK before calling it verified.

Each private research record should include: question, source fingerprints,
observed hierarchy/bindings, hypothesis, experiment, actual result, limitations,
chosen API/adapter and next check. Track evidence as candidate, statically
inspected, synthetic-tested, rendered, controller-verified or blocked. Do not
collapse those stages into a single ambiguous "supported" flag.

## Research batches and dependencies

| Batch | Assets/native seam to investigate | Result needed |
| --- | --- | --- |
| A: existing list repair | Current adapter plus `optionsetting` row/title state | Explain and repair fading, heading and paging before layering new features |
| B: roots and navigation | Full-screen hosting, `ingametop`, command/category navigation, key guides | Screen lifecycle, safe-area layout, tabs, focus and modal routing |
| C: primitive controls | Settings builders, `slider`, numeric/quantity selection, message dialogs | Native control value and commit/cancel contracts |
| D: data views | `inventory`, `itemlist_*`, `itemstatus_*`, scrollbars/providers | Generic bounded list/detail data, selection, filtering and redraw |
| E: HUD | `fe` and related stock groups, visibility and update sites | Owned passive mounts, anchors, layers, live snapshots and cleanup |

After A, B establishes shared foundations. C/D build on those; E can be researched
independently but must use the same ownership/layout/input contracts. This is
dependency planning, not authorization to create parallel agent tasks.

If a stock widget cannot render generic content or arbitrary layout, document a
narrow template contract and capability, or investigate an authored compatible
template. Do not hide the gap with ImGui, a noninteractive screenshot or six-row
paging presented as a complete inventory. An unsupported feature remains visible
in the coverage index with its reason and next experiment.

## Gallery structure

The entry point under Mods opens a full-screen gallery once supported. Its
L1/R1 top-level tabs are **Components**, **Layouts**, **Patterns**, **HUD** and
**Scenarios**. Each tab uses a navigable example list and detail/demo region, so
large collections of examples do not become dozens of shoulder tabs. Before the
full-screen adapter exists, keep a clearly labelled baseline menu for the
existing examples and progress; do not count it as full-screen acceptance.

| Gallery area | Demonstrations |
| --- | --- |
| Components | Label/help, action, toggle, choice, slider/stepper, text, icon/image, progress/status, prompt bar and dialogs |
| Layouts | Row/Column nesting, groups, split panes, bounded grids, clipping/scrolling, layers, full-screen roots and modal roots |
| Patterns | Tabbed settings, inventory list/detail, category/filter/sort, context actions, quantity/confirmation, staged Apply/Cancel and asynchronous feedback |
| HUD | Resource bar, status effects, cooldown, objective tracker, quick slots, shared anchor stacks and opt-in layout editor |
| Scenarios | Empty/long data, long labels, missing capabilities, unavailable data, removed focus, tab removal, live refresh and unload/reload |

Each example exposes a short purpose, current accepted value, controller prompts,
reset action, implementation source location and known capability limits. Show
normal/focused/disabled/pending/error states where relevant. Player-facing pages
explain behavior; raw bindings, addresses and debug logs belong in developer
evidence. Avoid hardcoding machine paths into packaged examples.

## Pattern recipes

**Tabbed settings:** group toggles, choices and sliders under several tabs. Show
immediate versus staged apply explicitly. L1/R1 changes content while preserving
pending edits, focus and per-tab position. Back follows the selected edit policy.

**Inventory browser:** use named categories, stable item keys, count/value columns,
details, filters/sort and a context menu. Quantity and confirmation dialogs change
only the mod-owned sample inventory. Label it simulated. Add optional read-only
carried-inventory browsing later; missing live capabilities cannot disable the
sample. Do not claim equip/drop/sell/transfer support from a working UI.

**Live list/detail tool:** generate changing observations without game writes.
Demonstrate removal of the selected item, duplicates by label, stable-key sorting,
loading/failure/retry and restoring focus after a child screen.

**HUD dashboard:** compose independent anchored groups from the same primitives.
Adjust simulated resource/cooldown/objective data in the gallery, then observe
passive HUD updates during gameplay. Full-screen settings control scale, opacity,
placement and visibility. An opt-in controller editor demonstrates explicit
capture, Apply/Cancel and restoration. It does not disable stock HUD automatically.

**Action workflow:** context action -> quantity -> confirmation -> pending ->
success/failure -> updated list and details. Use deterministic simulated failures
and requests to demonstrate stale-data rejection and duplicate-submit prevention.

## Build sequence and reviewable outputs

| Stage | Output in SDK/host | Output in UI-Examples-Mod | Acceptance gate |
| --- | --- | --- | --- |
| 0. Inventory and baseline | Repair existing list faults; establish capability/evidence ledger | Preserve counters, hide/show, label, values and reset; document current package/state | Existing functions still work in an isolated actual-game trial |
| 1. Screen foundation | Additive owned tree/mount services, layout constraints, semantic routing | Full-screen shell with two tabs, action, value feedback and Back | Controller-only complete interaction, correct heading and no leaked game actions |
| 2. Composition and focus | Nested containers, scoped focus, prompt service and modal stack | Layout pages plus tabbed settings and confirmation | Tab/modal transitions preserve focus; invalid updates retain accepted tree |
| 3. Controls | Verified native controls and copied event/value wrappers | Component state gallery and settings recipes | Value bounds, submit/cancel, failure feedback and unload ownership work |
| 4. Data browsing | Bounded generic lists/providers, detail and query state | Simulated inventory, live list and action workflow | Long datasets, revision changes, filtering and stable selection work |
| 5. HUD | Passive mounts, anchors, contexts/layers and interactive mode | HUD showcase, settings/editor and simulated timed values | Gameplay unaffected by passive HUD; capture/visibility/cleanup verified |
| 6. Integration | Compatibility, two-owner coexistence and bounded performance | Coverage index, guide, packaged build and dated evidence | Required recipes pass; unsupported optional features disclosed |

The shared contract covers Screens and HUDs at stage 1, even if HUD native
mounting lands later. Probe the HUD seam during research so layout assumptions
do not require an incompatible second architecture. Full native text entry,
virtual keyboards, radial menus, world markers and 3D previews can be independent
extensions; controller-only navigation/filtering must work without them.

## Verification and completion

Use focused tests for atomic publication, ABI size checks/older hosts, owner and
generation validation, transaction rollback, list recycling, focus/modal rules,
input repeats and cancellation. Verify actual example DLLs use the public API.
Use a minimal second-owner fixture for namespace collisions, region ordering and
independent unload; one mod alone cannot establish cross-mod isolation.

Prepare/build the runtime from canonical `src/host`, `sdk` and the authored
integration patch. Build/package the demo separately through the existing example
workflow; do not add a project-specific build system unnecessarily. Run relevant
existing checks and record actual commands/results. No deployment is implied by
this documentation task; retain rollback for any later requested installation.

In an isolated game trial, exercise controller-only entry, L1/R1, value changes,
scrolling, filters, dialogs, Back, reopen and active-view reload. Repeat with held
buttons, disconnect/focus loss, removed tabs/items, empty results and two owners.
Verify keyboard text separately. Check full-screen bounds, clipping, long labels,
prompts, HUD overlap/visibility and at least the baseline resolution; record any
unverified scaling/ultrawide configurations rather than assuming they work.

Measure tree/view allocations, visible-list/cache bounds, publication volume,
idle versus updating cost and repeated mount/unmount behavior. Set explicit
budgets based on the prototype before calling the design scalable; do not invent
performance pass thresholds after the final run.

Completion requires a source-to-page coverage matrix for every agreed core
component and recipe, reproducible build/package instructions, successful native
interaction evidence and a known-limitations list. Include lifecycle failures as
failures until resolved; passing synthetic tests cannot waive a broken visible
control. Update examples and SDK docs to actual signatures as features land.

Run publication audit and provenance review before staging. Keep generated game
references, executable profiles and trial captures/logs private; link public
documentation only to authored source and safe summaries. This plan does not
authorize deployment, gameplay writes, committing or publishing.
