# Implementation plan

Status: planning only. Milestones are ordered by dependencies, not time estimates.
Do not start bulk example migrations before the first composed slice works.

The concrete deliverable is **UI-Examples-Mod**, specified in the
[delivery and investigation plan](ui-examples-mod-plan.md). Each milestone should
add a reviewable page/scenario to that mod as well as reusable SDK functionality.

## 1. Establish evidence and repair the baseline

- Reproduce the existing fading/paging and incorrect System heading in an
  isolated build. Inspect native bindings and movie row states/masking/opacity;
  record the actual cause rather than assuming disabled state.
- Export a small private GFX sample set with tool versions and original hashes.
  Produce searchable object trees, frame/state names, script/native bindings and
  dependencies. Keep original assets untouched and exports outside public docs.
- Compare native settings/list/slider builders with pinned upstream references.
  Confirm calling conventions, ownership, thread context and executable profiles.
- Investigate full-screen root hosting and tabbed category/list screens early.
  Determine whether reusable templates, generated overlays or new native bindings
  are needed for each layout. No claim of generic layout until demonstrated.
- Investigate stock HUD (`fe.gfx`) mounting, layout, shared resources, visibility
  and native update lifetime independently from Screen factories.

Exit: evidence identifies the baseline defects and a candidate full-screen seam;
the corrected baseline has visible navigation/action/feedback/Back verification.

## 2. Specify additive host and Rust contracts

- Define Screen presentation, layout tree, component IDs/properties, transaction
  boundaries, typed events, capability queries and unsupported errors.
- Define controller actions, scope priority, foreground arbitration, focus,
  navigation history and modal capture. Audit existing shared input suppression
  before introducing another consumer.
- Keep legacy ABI behavior; append size-gated services and owned resources.
  Specify publication limits, unload cancellation and serialization versions.
- Define revision-consistent list providers and stable domain keys without
  coupling UI components to gameplay structs.
- Define passive HUD mounts, anchors, shared-region/layer arbitration, visibility
  observations, animation clocks and explicit interactive capture.

Exit: reviewed public signatures and lifecycle diagrams; proposed Rust usage is
clearly labelled until implemented. Test ABI compatibility and rejected updates.

## 3. Prove a full-screen composed slice

Implement one full-screen Screen with Header, Column/Row or SplitPane, TabView,
small selectable list, details and ActionBar. Use mod-owned sample data first.
L1/R1 switches tabs; each tab remembers selection. Add one modal confirmation.
Use verified native layout templates initially if arbitrary layout is unavailable;
publish their constraints and capabilities explicitly.

Exit: controller-only open -> tab -> select -> action -> feedback -> modal Back ->
close -> reopen works in-game. Verify full-screen viewport coverage, correct
titles, safe areas, controller prompts, input isolation and two screen owners.
Registration or a static screenshot alone does not pass this milestone.

## 4. Add native controls and scalable browsing

Implement native Slider/Choice bindings, ScrollRegion, VirtualList, DetailsPanel,
filter/sort and QuantityDialog in dependency order. Reuse existing system text
ownership for optional search; controller-operable non-text filters are required.
Use bounded view/cache state and stable selection through revision changes.

Exit: long synthetic datasets demonstrate bounded allocation and responsive input;
actual game rendering verifies scroll clipping, focus reveal and state styling.
Test cancellation, stale rows, partial failures and live data removal.

## 5. Integrate reference interfaces and migrate examples

- Build the read-only inventory reference against current carried-inventory
  capabilities, plus a mod-owned dataset demonstrating reusable composition.
- Build tabbed settings and one observer/tool interface without gameplay writes.
- Add the passive HUD showcase and opt-in controller layout editor. Validate
  coexistence with stock HUD, foreground screens and a second mod owner.
- Preserve existing example functionality/state while migrating; do not replace
  editable tools with summaries or links to debug UI.
- Complete a safe native F1 entry using the same screen router. Game-wide
  keyboard/mouse accessibility remains outside this work's established scope.

Exit: reference walkthroughs pass on controller, with recorded limitations for
unavailable gameplay operations and keyboard-only text typing.

## 6. Verify lifecycle, compatibility and release

| Evidence | Required coverage |
| --- | --- |
| Host/SDK tests | Atomic updates, ownership, nested unload, modal cancellation, stale events, conflicting foreground requests, old-host Unsupported behavior |
| Input/focus tests | Scoped L1/R1 routing, repeat/held buttons, disconnected devices, hidden/disabled/removed nodes, per-tab restore and Back order |
| Data tests | Stable keys, changing revisions, recycled rows, empty/error/loading states, bounded virtualization and failed operations |
| Asset checks | Original hashes preserved, generated overlays reproducible, required symbols/fonts/textures available, title/mask bindings checked |
| Isolated runtime build | Prepared from canonical host/SDK plus integration patch; matching capability/profile checks |
| Actual game interaction | Full-screen/tabs/list/control/modal/return behavior, no game-input leakage, visible live updates and reload/unload |
| HUD interaction | Anchors, live updates, visibility contexts, passive input, interactive capture, two-owner region stacking and independent cleanup |
| Visual review | 1080p, another output scale and ultrawide when supported; long/localized labels, clipping, prompts and focus/disabled contrast |

Build/test separately from installation. No deployment is authorized by this
plan. A later requested deployment retains hash-checked rollback. Run publication
audit and provenance review before staging public changes. Record synthetic,
build, rendered interaction and gameplay-operation evidence separately.

## Decisions to resolve through the first prototype

- Which verified native root/template supports full-screen regions and layering?
- Can stock list/category controls use generic providers, or do we need a narrow
  generated template with explicit layout limits?
- What tree/view limits fit measured costs without inheriting the old registry's
  node budget as an assumed full-screen design?
- Which screen contexts can capture input safely, and which can actually pause?
- What text-entry experience can be supported on a controller without typing?

These are research decisions, not blockers to documenting the component model.
