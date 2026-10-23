# UI work: next agent handoff

Saved 22 October 2026 at the user's request. Work is on disk, uncommitted and
unstaged. This is the current checkpoint; older entries in
[IMPLEMENTATION-HANDOFF.md](IMPLEMENTATION-HANDOFF.md) preserve historical trials.

## Objective and exact stopping point

Build composable native UI components for the SDK and demonstrate them in
UI-Examples-Mod: full-screen Screen roots, list/detail browsers, L1/R1 tabs,
controls, dialogs and separately mounted HUD groups. The immediate approved
task is to trace inventory composition, then prove a read-only browser using
mod-owned sample data before using real inventory data.

**The native browser is not implemented or rendered yet.** This session added
passive composition diagnostics, a renderer-independent Rust list state core,
and an authored browser-data test. The user asked to save before a full runtime
rebuild, fresh trial preparation or launch. Do not treat this as a completed
full-screen milestone or show the existing settings list as an inventory UI.

## Update, later on 22 October: sample browser on native menu rows

`docs/examples/ui-examples/src/browser.rs` adds a read-only "Sample browser"
screen under the dashboard, using mod-owned data and one `ListState` per
category. A category action cycles Equipment/Notes (stand-in for L1/R1, which is
still unrouted). Activating an entry selects it and updates a detail Info row;
entry help text also carries the details. Entry rows are released/re-registered
on category change. This is the existing five-slot native menu, **not** the
stock inventory composition, a full-screen layout or shoulder-button routing.
Checked: `cargo test --locked -p bbmod -p ui-examples` (43 SDK, 4 example, 1
integration) and the actual-DLL `native_ui_examples` lifecycle test (row counts,
detail changes, category switch, 40 reloads) via `build.ps1 -Test`; publication
audit passed (245). Packaged in `dist/example.ui-examples`. **Not run:** live
game trial. Native keys allow only `[A-Za-z0-9._-]` (a colon was rejected).
Game and launcher processes were still running, so no trial was prepared or
launched; native inventory tracing (step 4 below) still needs a user-driven run.

### Live trial, 22 October (user-observed)

Trial `20261022T090425856845Z` (`--components`, fresh runtime build). User: stock inventory works; **Left/Right changes tabs, L1/R1 does not**; Sample browser works. Trace summary is private in `.local/ui-research/inventory-native/live-trace-20261022.md`: inventory = two type-3 Steps from one factory per open; mod screens use separate type-7 factories. Next: Left/Right category switching in the browser (needs input route check), then the factory/provider experiment. Game closed afterwards.

## Update, 23 October: overlay layer, Mod Menu and settings are built (read this first)

Built and wired end to end; the **user confirmed on 22 October that the Mod Menu looks and works
well in the game** (F2 / pad combo, tabs, list, settings, Difficulty). Not yet user-tested:
the pause-menu "Mods" takeover below (needs the 23 October runtime rebuild and a fresh trial).

- **ABI 1.11** (`sdk/include/bbmod.h`, mirrored in `bbmod-sys`): `ui_layer_set` (retained 'BBUI'
  draw list, HUD/SCREEN/MODAL), `BBM_EV_UI_ACTION`, settings (`setting_declare/get/set`,
  `BBM_EV_SETTING_CHANGED`) and the manage capability (`mods_info`, `mod_request`,
  `settings_info`, `settings_write`; manifest `[mod] manage = true`). Host: `src/host/src/ui_layers.c`
  (validation, 60 Hz fade, foreground arbitration, lock-free capture flag), `settings.c`
  (validation, persistence to `config/<id>.settings.toml`, owner events), tests
  `test_ui_layers.c` (fake mods) and `test_menu_mod.c` (real Mod Menu + Difficulty DLLs).
- **SDK**: `bbmod::ui_layer` (`Screen`, `HostScreen`, encoder, nodes, `UiLayers` reference model),
  `bbmod::settings` (`Def`, `Value`, `declare_setting`, `Event::setting_changed`, manager calls).
- **bbport**: renderer and input routing are in the integration patch (regenerated; backup of the
  old one in `.local/backups/`): `bbport_modui.cpp` draws streams with ImGui into the background
  draw list, fitted to the game frame; `bbport_overlay.cpp` maps keyboard/pad/stick to actions with
  hold-to-repeat and makes `CapturesInput()` include mod layers. The runtime stamp was updated in
  place after `git apply --reverse --check` and a forward apply on a pristine worktree.
- **Mods**: `src/manager` (Mod Manager, formerly `mods/bbmod.menu`) and `../modding/bb-modding-difficulty` (own repo) (publishes all 15 keys as host
  settings; host values win over its own file unless still default). Build each with
  `cargo build --release --manifest-path mods/<mod>/Cargo.toml`, then prepare a trial with
  `python src/scripts/prepare_ui_trial.py --extra-mod src/manager --extra-mod ../modding/bb-modding-difficulty`.
- **Pause menu**: a registered Action `mods-root` under `game.system` (allowed only for `manage`
  mods) replaces the built-in "Mods" link (`root_item` in `native_menu_bridge.c`), so the pause
  menu's Mods entry opens the Mod Menu. Unloading the Mod Menu restores the native Mods list.
- **Known limits**: text uses the overlay's font and prompt icons are labelled badges (no game
  font/TPF decode yet); no mouse routing; HDR output may need a pipeline rebuild (the overlay
  pipeline uses the initial swapchain format); Text/Keybind settings edit via the system dialog and
  a key picker. Dependencies are ordering only (`after`), so the menu warns instead of cascading.
- **Checks run**: 25 host tests (all pass), 63 SDK tests, 7 menu and 41 Difficulty mod tests,
  4 trial-script tests, publication audit (259). Live: user ran the 22 October trial (F2, tabs,
  settings, unload/load). Gameplay effect of Difficulty settings through the menu was not verified.

## Update, 22 October (late): `bbmod::ui_layer` and overlay design

Design report: [overlay-layer-design.md](overlay-layer-design.md) (investigation only; no
spikes, no screenshots). Decision: a native retained Rust UI drawn through bbport's
existing present-path overlay hook (`BbOverlay::Render`), 60 Hz UI clock, stage canvas
fit like the game frame. ImGui stays as the fallback.

SDK: `sdk/rust/bbmod/src/ui_layer/` (pure Rust, no host calls, no ABI change): `Node`
tree (panel, row/column/layer layout, text, icon, bar, button, toggle, slider, choice,
list via `ListState`, tabs, key guide), `UiLayers` (HUD = passive; Screen/Modal =
interactive with one foreground input owner, focus repair, generation-checked
`Handle`s, `release_owner`), `UiClock`/`Presence` (60 Hz ticks, 30 fps-authored fades,
smooth or stepped), `Canvas::fit`, and `DrawCmd` output. Checked: 13 `ui_layer` unit
tests inside `cargo test --locked -p bbmod -p ui-examples` (56 SDK tests total pass).
**Not built:** the host renderer (consume `DrawCmd`, glyphs, TPF textures, ABI to submit
trees and forward input), so nothing from `ui_layer` appears in the game yet. Text
widths use a rough estimate until a real font measure exists. The Sample browser still
uses `native_menu`.

## Read first and preserve

- Workspace `AGENTS.md`, toolkit `AGENTS.md`, `README.md`, `docs/SETUP.md`,
  `docs/CONSOLIDATION.md`, `.agents/skills/bb-modding/SKILL.md` and the available
  personal `game-ui` skill, including its native-integration reference.
- [GFX survey](gfx-survey.md), [architecture](architecture.md),
  [implementation plan](implementation-plan.md) and
  [reference interfaces](reference-interfaces.md).
- Canonical code is `src/host`, `sdk`, `src/scripts`. Prepare/build
  `tools/bloodborne_pc` through the pinned integration workflow. Do not edit its
  materialized host/SDK copies independently.
- Original game files, installation, packages, saves and settings are preserved.
  Build and run isolated trials; no installed runtime deployment is requested.
- There are extensive pre-existing unrelated dirty changes from consolidation.
  Do not reset, bulk-stage or commit them. Private game-derived research remains
  under ignored `.local`; no recovered code/assets in public source.
- No other agent/chat has been messaged. The user will arrange the handoff.

## Source changes in the latest session

| File | What changed |
| --- | --- |
| `src/host/src/native_menu_bridge.c` | Passive component observation using existing Step constructor, list-input and asset resolver hooks. Logs Step name/type, safely read factory descriptor words expressed as image RVAs, active Step changes, movie requests and changed selection. Atomic 512-record budget. No extra hooks or movie/gameplay calls. Cleared on shutdown. |
| `src/scripts/prepare_ui_trial.py` | New `--components` flag / `components=True` argument. Explicitly sets `BB_MODS_NATIVE_UI_COMPONENT_TRACE=1`; default sets 0 to prevent inherited opt-in. Separate from existing `--trace` slot-state logging. Records the flag in `trial.json`. |
| `src/tests/test_native_menu_guest.c` | Exercises unreadable/empty factory descriptors and bounded diagnostic budget. Existing native adapter regressions still run. |
| `src/tests/toolkit/test_prepare_ui_trial.py` | Default-off and explicit component observation tests, independent of slot tracing. |
| `sdk/rust/bbmod/src/ui_state.rs` | New public, pure Rust `ListState<K>`; no host/native calls or ABI changes. Newer snapshots require unique keys and monotonically increasing revisions. Preserves selected keys on reorder; nearby fallback on removal; empty state; stale event rejection; bounded visible range and focus reveal. One instance per tab retains each tab's state. |
| `sdk/rust/bbmod/src/lib.rs` | Exports `ui_state`. |
| `docs/examples/ui-examples/tests/browser_state.rs` | Nine invented equipment entries and two notes; checks independent category selection, seven-row viewport, details lookup by stable key, reorder, empty category and stale events. Test fixture only: not wired to the running example or native shoulder input. |
| `docs/ui-components/README.md`, `IMPLEMENTATION-HANDOFF.md`, this file | Current boundary and resume instructions. |

The SDK core is usable for state only. It does not create native controls,
capture input, virtualize engine objects or publish snapshots into the host.
An eventual native adapter must copy retained keys/text/payloads into host-owned
storage and validate owner/generation on actions and reload.

## Checks actually run on this checkpoint

- Targeted C build and `native_menu_guest_adapter`: **passed**.
- Trial preparation Python tests: **4 passed**.
- `cargo test --locked -p bbmod -p ui-examples`: **31 SDK unit tests, 2 example
  unit tests and 1 browser integration test passed**; 3 doc examples ignored.
- Publication audit: **243 candidates passed**, with private outputs excluded.
- **Not run after these changes:** full runtime build, release DLL packaging /
  actual-DLL lifecycle harness, new game launch or native browser interaction.
  The existing runtime-stage and dist package predate this checkpoint.

## Native research saved privately

Use `.local/workspace.json` for configured input paths. This machine's executable
inspection input was the installed port's `out/eboot.elf`. Addresses in the private
notes are already game RVAs; do not add a presumed image base.

Latest directory: `.local/ui-research/inventory-native/`:

- `string-xrefs.json`: executable SHA-256, ASCII/UTF-16 string locations and
  candidate RIP-relative LEA/MOV references. Byte-pattern candidates need bounded
  disassembly confirmation; this is not a complete disassembler xref database.
- `findings.json`: concrete constructor/resource/call-site observations with
  unresolved assumptions explicitly marked.
- `detail-bind.asm`, `inventory-entry.asm`, `movie-registration.asm`: private
  bounded disassemblies. Some ranges begin mid-instruction; follow confirmed
  function starts / call sites before interpreting them.
- Existing `.local/ui-research/relative-relocations.json` maps decimal-address
  strings to relocation targets. Read it programmatically and select entries;
  do not print the entire multi-megabyte file.

Useful tools: `src/scripts/generate_profiles.py::segments` reads ELF/unencrypted
SELF. LLVM objdump is available in the MSYS2 CLANG64 bin directory. Python
Capstone was not installed. A bounded objdump command with the converted ELF
prints a misleading section-overlap warning but still disassembles its synthetic
PT_LOAD section; verify instruction boundaries.

Static findings support separate shell/category/list/detail resources. They
do **not** establish a callable safe custom inventory factory or provider ABI.
Trace factory allocation, hidden returns, callback copies/destruction, selected
row provider, detail updates, focus and Back before invoking anything new.
Keep executable addresses and generated evidence private.

## Existing verified baseline: do not regress

- The running native menu uses `optionsetting.gfx` with **five physical rows**.
  Long logical menus use **three content rows plus up to two navigation rows**.
- Paging brightness was verified by the user on 21 October after multiple
  failed trials. Fix: wait for all current rows to be visible and alpha >=99.99
  before changing physical count. Keep old rows / pending revision while waiting.
  Then use native count/focus/layout/redraw/selection. No alpha or timeline setters.
- Trace evidence: `.local/ui-trials/20261021T104541082088Z/evidence/`.
  Twenty count changes corroborate full alpha and settled positions.
- Native getter wrappers differ: movie value 0x38 bytes; row wrapper 0x60 with
  embedded movie value at +0x28. The first incorrect tracer crashed the game.
  Current typed layouts, release paths, canary regression and fingerprints must
  stay intact. Do not treat arbitrary native objects as Scaleform values.
- `System` heading is still unresolved. Existing counters, hide/show settings,
  Choice/Integer child menus and Text control remain in the example.
- `Kind::Screen` currently means a logical settings-menu destination; generic
  full-screen composition, tabs and HUD mounts remain unimplemented.

## Next actions, in order

1. Review the latest diff and private findings. Finish the factory/provider map
   using bounded executable inspection. Compare `weaponupgrade` composition to
   inventory, but do not inherit equip/upgrade/consume gameplay callbacks.
2. Build a fresh separate runtime from canonical source. Rebuild/test/package
   UI-Examples-Mod so the stage and package manifests represent this checkpoint.
   Typical commands from the toolkit root:

   ```powershell
   . ./src/scripts/dev-env.ps1
   ./src/scripts/build-runtime.ps1
   ./bb.ps1 build-mod docs/examples/ui-examples --test
   python src/scripts/prepare_ui_trial.py --components
   ```

   MSYS2 native build commands have needed elevated sandbox execution here to
   create signal pipes. Use the normal tool approval mechanism if needed.
3. Check existing `Bloodborne` / `bb-probe` processes. At the last check, game
   and launcher processes were still present under the installed runtime path.
   Do not kill them. Ask the user to close normally before launching the fresh
   trial. `prepare_ui_trial.py` creates a new private directory, copies settings
   and saves, verifies hashes and does not launch. Its generated launcher refuses
   to start while another game/launcher is running.
4. Launch the generated `launch.ps1` using hidden PowerShell. Ask the user to
   open the **stock inventory**, navigate categories with L1/R1, scroll beyond
   seven entries, change detail view if available, Back and reopen. Leave it
   open to inspect `bbmods.log` entries tagged `bbmod.ui.components`. The new
   trace records requests, creation and focus; these are **not rendering proof**
   or proof of generic providers. Correlate timestamps and manual observations.
5. Use that evidence to implement the smallest read-only native browser with
   sample data, explicit safe Back and host-owned lifetime. Resolve the title.
   Prove entry -> list selection -> changed details -> Back -> reopen in-game
   before promoting any native Screen/list/detail public APIs.
6. Then add category routing, L1/R1 through the existing foreground input owner,
   per-category `ListState`, correct prompts, modal behavior and reload cleanup.
   HUD research uses `fe` separately; it should be passive by default.

Keep the user informed, report actual test scope, and update this checkpoint
with precise live evidence before describing the browser as implemented.

