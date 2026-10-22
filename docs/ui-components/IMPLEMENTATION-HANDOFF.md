# UI implementation handoff



Updated 22 October 2026 after the native paging trials and GFX survey.

Changes are saved on disk, uncommitted and unstaged. Preserve the

workspace's pre-existing unrelated changes; do not reset or stage everything.



## Latest handoff — 22 October 2026, inventory composition work

The user paused development to hand this work to another agent. Start with
[NEXT-AGENT.md](NEXT-AGENT.md), which records the current source changes,
checks, private native research, unfinished renderer and exact next steps.
The diagnostics added in this session have **not** been included in a rebuilt
full runtime or launched in-game. Existing runtime-stage/dist packages predate
these changes. Do not describe the new browser as rendered or controller-tested.

## Previous resume point — 21 October

22 October research update: [GFX candidate survey](gfx-survey.md) covers all 128
loose GFX files under the configured game root (112 distinct hashes; 105 distinct
basenames; 21 font files). Both inspectors parsed all files, external-image names
agreed, no canonical frame warnings occurred, and original hashes stayed unchanged.
The user-supplied ZIP's named-tree parser was reviewed and called read-only; its
skill was not installed. Private evidence is
`.local/ui-research/gfx-survey-20261022T075934314268Z/manifest.json`.
Strong candidates are the integrated `weaponupgrade` composition and the separate
`inventory` shell / seven-row `itemlist_*` / `itemstatus_*` families. Inspect their
native composition before extending the generic full-screen API. `fe` remains
the separate HUD mounting target. The public survey records specific controls,
uncertainties, supplied-tool checker gaps and the next native experiment. No game
launch or runtime/source binary changes were made for this survey.



Paging brightness is now verified in the isolated game. The user reported

**all rows stay visible and bright**, after repeated small/large-page transitions

including quick changes. The trace records twenty row-count changes; every one

has all active slots visible at 100% alpha and settled positions after updating.

Evidence: `.local/ui-trials/20261021T104541082088Z/evidence/verified-paging.json`

and its hashed `paging-trace.log`. The installed runtime was not replaced.



### Implemented fix



Native actions can restart the list entrance fade before the next widget update.

Changing from four rows to two mid-fade hid the trailing slots at zero alpha and

a vertical offset. Reusing them restored visibility but stranded those transforms.

The live adapter now waits until all currently displayed rows are visible at

>=99.99% alpha before changing physical row count. It retains the pending

revision and old rows while waiting, then uses count/focus/layout/redraw and help

selection. Initial entrance and same-count updates remain native. No keyframe or

alpha setters are installed. The stock Top has five slots, so pagination uses

three content rows and up to two navigation rows.



The native row getter returns a 0x60-byte wrapper with its 0x38-byte movie value

at +0x28. Separate typed layouts, compile-time assertions and a regression with

invalid row-header canaries enforce this distinction. Managed movie values are

released on every path. Five additional instruction fingerprints cover narrow

list updates and the row getter; stale live profiles fail closed. Tracing is

optional (`prepare_ui_trial.py --trace`); normal trial preparation disables it.



### Actual checks



- Separate runtime build passed; stage is `.local/runtime-stage`.

- Native guest adapter regression passed, including fade deferral/retry, stable

  IDs, all pages, hidden/invalid values, slot bounds and managed-value release.

- UI-Examples-Mod's two Rust state tests and actual-DLL lifecycle harness passed.

- Three isolated-trial preparation tests passed, including trace opt-in and

  preserving original files and independent copied saves.

- Publication audit passed with 239 candidates; manual provenance review remains.

- Earlier in this session: full native suite 28/28, SDK workspace tests, Python

  suite and stock asset checks passed. These preceded the final fade gate; do not

  describe them as a full-suite rerun of the final code.

- Gameplay evidence above covers menu entry and repeated paging. Earlier user

  checks covered example controls. Controller-specific behavior and every reload

  path are not newly gameplay-verified by this paging trial.



### Research, packaging and next work



`bb.ps1 ui-inspect` exports bounded GFX structure/provenance into private reports.

Optional `--decompile` uses hash-pinned JPEXS 26.4.0; CLI/decompiler exports and

source preservation were exercised. Original game assets remain read-only.



The example package is `dist/example.ui-examples`, built using

`bb.ps1 build-mod docs/examples/ui-examples --test`. It retains existing state and

controls; the gallery's full-screen Screens, shoulder tabs, inventories and HUD

mounting remain proposals. The System heading is still an open native binding

issue. Next work should resolve that heading, then investigate the first native

full-screen/tab slice, following the architecture's rendered-verification gates.



No staging, commit, push or installed deployment was performed. Preserve the

workspace's unrelated changes. Fresh trials use copied settings/saves and only

this example mod; only the trial disables the unrelated debug-menu font dependency.



### Failed attempts retained privately



- `20261021T095541823564Z`: narrow list refresh alone did not resolve fading.

- `20261021T100616384108Z`: Loop keyframe reset did not resolve disappearing rows;

  the setter was removed.

- `20261021T101351990084Z`: first tracer crashed because it treated a row wrapper

  as a movie value and dispatched through invalid fields. Do not run this trial.

- `20261021T102702215432Z`: rollback restored menu entry; paging remained broken.

- `20261021T103856701309Z`: corrected tracer reproduced zero-alpha reused slots

  without crashing and established the fade/row-count timing failure.

- `20261021T104541082088Z`: current fixed trial, positive user/trace evidence.



The sections below preserve the original 20 October checkpoint. Its pending

checks and pause state are historical and are superseded by the update above.



## Objective



Implement the [UI-Examples-Mod plan](ui-examples-mod-plan.md): reusable native

components, full-screen Screens, L1/R1 tabs, composed inventory/tool interfaces

and HUD surfaces. Follow the architecture and the staged native-verification

gates. Full-screen, tabs and HUD mounting have not been implemented yet.



## Changes made in this session



- Added `src/scripts/inspect_ui.py`, a bounded read-only parser for uncompressed

  GFX/FWS structural research. Exports JSON per movie, searchable placements TSV

  and a provenance manifest with input/tool hashes. Records timeline labels,

  placements, matrices, RGBA transforms, clip depths, symbols/imports, external

  images and bytecode locations. It does not render, evaluate timelines or

  decompile scripts. Unknown tags/placement tails remain explicitly uninterpreted.

- Wired `bb.ps1 ui-inspect` with optional `--movie`, `--game` and `--out`. CLI

  output must be in a new child directory under ignored `.local/ui-research`.

  Existing reports and original game inputs are preserved. The direct inspector

  ran successfully; the toolkit dispatch has not yet been exercised.

- Added six authored synthetic parser/export tests in

  `src/tests/toolkit/test_inspect_ui.py`.

- Static inspection of the local stock `optionsetting.gfx` found that Top has

  five command slots (`Item_0_0` through `Item_4_0`). Other settings sprites have

  different slot counts. The live adapter had assumed six Top command slots.

- Changed `MH_MENU_VISIBLE_ROWS` from six to five and paging from four to three

  content rows plus up to two navigation actions. Updated the guest-adapter

  regression for five pages of thirteen logical rows and stable-key reordering.

  This is a slot-count correction, not a proven fix for the visual fading.

- Added a private-input test in `test_native_menu_assets.py` tying the actual

  stock Top capacity to the host limit; it has not yet been run.

- Changed the existing `example.ui-examples` display name/dashboard to

  UI-Examples-Mod, keeping its crate/DLL/mod ID and UIEX state format intact.

  All existing controls and state logic remain. No new gallery pages yet.

- Added `docs/examples/ui-examples/build.ps1` to build/test/package separately,

  including actual DLL lifecycle checks through the C harness. This new script

  is not fully verified and has not produced a completed package this session.

- Corrected the example's C integration wrapper include path to canonical tests.

- Updated architecture/catalogue/reference interfaces to include HUD surfaces,

  anchors, live updates, visibility/layers and explicit interactive input capture.



## Evidence and validation



| Check | Actual result |

| --- | --- |

| Inspector synthetic tests | 6/6 passed |

| Existing UI example Rust tests | 2/2 passed |

| UI example release DLL compilation | Passed during the package attempt |

| Publication audit | Passed: 231 candidates; manual provenance review still required |

| Native slot-count/private asset test | Added, not run |

| Changed guest-adapter test / full native suite | Not run |

| Example build/test/package script | Started, stalled during CMake compiler ABI detection; interrupted when user asked to pause |

| Runtime preparation | Completed; canonical modified host copied into prepared runtime |

| Separate full runtime build | Failed before compilation: MSYS2 bash could not create a signal pipe, Win32 error 5 |

| Game launch/rendered interaction/deployment | None this session |



The seven inspected movies are optionsetting, slider, commandlist,

itemquantityselect, inventory, ingametop and fe. Private reports are under

`.local/ui-research/20261020T101909563006Z/` (manifest, placements.tsv, seven JSON

files). All parsed without frame-count warnings. Static structure does not

establish native rendering or behavior. GFX originals were opened read-only.



The old installed runtime and saves/settings were not replaced. The existing

runtime stage may still be from the earlier consolidation build: do not treat it

as containing today's slot-count correction. Materialized source can be refreshed

through the normal preparation workflow; never independently edit its copies.



The package attempt used a separate CMake tree at `.local/ui-examples-native`.

The exec session was interrupted with Ctrl+C. Process inspection through

Get-CimInstance was denied by the environment, so descendants were not

independently enumerated. Check for a lingering owned build before rerunning;

do not stop unrelated compiler processes.



## Next steps when resuming



1. Read current repository instructions and this handoff; inspect Git status.

   Review new inspector/build script code before relying on it.

2. Exercise `bb.ps1 ui-inspect --movie optionsetting`; verify its CLI error paths

   and private-output safeguards. Keep exports private. Use FFDec separately for

   readable scripts/previews; it has not been installed or run in this session.

3. Run `test_native_menu_assets.py` with `BB_GAME_ROOT` set from configured game

   root. Run the changed guest-adapter test and the actual-DLL UI example tests.

   Resolve the CMake/MSYS2 sandbox/build-launch issue without changing the

   working installation or deleting unrelated build/source trees.

4. Finish the package script, correct the example README's historical build

   path/display name, and create an accurate package/validation record only after

   its checks actually pass. Inspect whether the C include wrapper compiles too.

5. Rebuild the isolated runtime. Validate the five-slot change in-game before

   claiming a visual repair. Investigate the remaining System heading and fading

   through movie state/binding evidence; opacity records alone do not prove cause.

6. Continue native full-screen root/tab research, then the first composed gallery

   slice. Reuse public SDK services and preserve example behavior/reload state.



No commit, push or deployment was performed. The planned APIs remain proposals.

