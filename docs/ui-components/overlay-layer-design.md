# bbmod overlay UI layer: investigation and design

Date: 22 October 2026. Exploration only. **No spikes were built or run, no game was
launched for this report, no screenshots exist, and no game or runtime files were
modified.** Every claim is marked **[V]** verified (evidence named), or **[A]** assumed
(not checked, or from memory). Paths are relative to `bb-modding/tools/bloodborne_pc`
(the pinned runtime checkout) unless noted. Do not edit that checkout directly; change
canonical source and re-prepare.

## 1. Summary and recommendation

**The overlay layer largely exists already.** bbport draws a Dear ImGui overlay in the
present path, gates game input through one flag, and bbmod already hosts a "Mods"
window plus mod panels in it. [V] So the task is not "find a hook point"; it is "replace
the look and the widget model of an existing layer without touching its hooks".

**Recommendation: option B, a native retained UI in Rust, drawn through the existing
hook, styled from design tokens and assets extracted at runtime from the user's game.
Keep ImGui (option C) as the developer fallback and as the interim layer. Do not adopt
Ruffle (option A) as the primary path.**

Reasons, in order of weight:
1. Fidelity needs only the game's art, font and measured geometry, not its Flash
   runtime. All three are obtainable. [V] for colours/sizes/timing (section 4).
2. Ruffle would add a second GPU stack (wgpu) beside the Vulkan renderer, a Java build
   dependency for its AS3 builtins, and GFx-specific preprocessing, for a benefit
   (authoring in Flash) the project does not otherwise use. [V Java requirement, A the rest]
3. The existing layer's input gate and unload-safe host ownership are reusable as they
   are; a retained Rust UI can sit behind the same `ui_panel`/`ui_contribute` ABI.

### Frame and input flow

```
 game GPU frame (internal res)
   -> upscaler / frame generator (RGBA8 "ColorFormat")
   -> blit-fit into swapchain image (MakeImageBlitFit)         vk_presenter.cpp ~1040
   -> BbOverlay::Render  [present thread]                      draws at DISPLAY res
        ImGui today; retained Rust UI would draw here instead
   -> transition to PRESENT -> vkQueuePresent

 window thread (SDL events) --> BbOverlay::HandleEvent --> UI input queue
                                      |
        bbgpu_overlay_captures_input() == true while open
                                      v
 pad HLE (runtime_pad.c sample_host / sample) returns NEUTRAL pad, game sees nothing
```

### Requirements added by the user (22 October)

1. **The UI runs at 60 fps** (corrected from an earlier "30 fps" message) and
2. **scales with resolution** the way the port already presents the game.

Design consequences:

- **Fixed 60 Hz UI clock, decoupled from the present rate.** The port may present above
  60 fps, including generated frames [V: overlay is drawn on every presented image,
  `MaxGenerated`]. UI state advances in fixed 1/60 s ticks from a monotonic clock, not
  per presented frame and not from `DeltaTime`, so behaviour is identical at 60, 120 or
  144 Hz presents. Presents faster than 60 Hz redraw the latest state; presents slower
  than 60 Hz run several ticks (catch-up capped, say 4, to avoid a spiral after a hitch).
- **Native animation data is 30 fps.** [V: 102 of 103 surveyed movies are 30 fps.] Frame
  counts taken from the movies (FadeIn to Loop = 5 or 6 frames) are therefore durations
  of 167-200 ms. Keep those durations, but evaluate them on the 60 Hz clock with
  interpolation between the authored 30 fps frames (each authored frame spans two ticks),
  so motion is smooth at 60 fps while totals match the native menus. A "stepped"
  option that holds each authored frame for two ticks reproduces the original exactly.
  Which looks better next to the real menus is a spike question. [A]
- **Input is applied on the UI tick,** so navigation repeat is the same regardless of the
  present rate. Repeat delays should be measured from native menus (not yet measured).
- **One virtual 1920x1080 canvas, uniformly scaled into the same fit rectangle as the
  game.** [V] `MakeImageBlitFit` calls `FitImage(frame w/h, swapchain w/h)`, an
  aspect-preserving fit; the game frame occupies that rectangle and the rest is black.
  The overlay should compute the same rectangle and use `scale = fit_height / 1080`
  with the fit offset, so UI sits exactly where the game's own menus sit at any window
  size, including ultrawide (pillarbox) and tall windows (letterbox). The current ImGui
  overlay instead scales only by `extent.height / 1080` from the top-left corner (`Render`),
  which is wrong on non-16:9 outputs. [V]
- **Scale vector and text, not bitmaps:** rasterise glyphs at the scaled pixel size (font
  atlas regenerated on a scale change, not on every frame) and snap panel edges to whole
  pixels to avoid shimmer; textures use linear filtering like the game's own blit. [A]
- Clipping to the fit rectangle is optional; the UI normally stays inside it.

## 2. bbport findings

### Render hook [V]
- `gpu/shadps4/video_core/renderer_vulkan/vk_presenter.cpp`: `Presenter` constructor calls
  `BbOverlay::Init(instance, swapchain.GetSurfaceFormat().format, images * (MaxGenerated+1))`
  (~line 164). In the present function (~1040-1090): after the game frame is blitted into
  the swapchain image, `BbOverlay::NotePresent()`, `Visible()`, then, if visible, the image
  goes to `ColorAttachmentOptimal`, `BbOverlay::Render(cmdbuf, swapchain.ImageView(), extent)`
  is recorded, then a barrier to `PresentSrc`. When invisible the overlay costs nothing but a flag read.
- The overlay draws over every presented image, generated frames included. Its vertex
  buffers are sized for `num_images * (MaxGenerated + 1)`.
- `gpu/shim/bbport_overlay.cpp` `Render` uses dynamic rendering (`beginRendering`,
  `loadOp = Load`), takes `Scheduler::submit_mutex` around draw submission (font atlas
  uploads submit on the graphics queue themselves), and holds `imgui_mutex`.
- The existing hook point is therefore reusable as is. A different renderer needs only
  a command buffer, the swapchain `ImageView`, and the extent.

### Resolution and canvas [V]
- The overlay renders at **display** resolution, after the blit, not at the game's
  internal resolution. UI scale is `max(extent.height / 1080, 0.75)` (`Render`), so a
  virtual 1080p canvas is already the convention. [V]
- Gap: the game frame is letterboxed by `MakeImageBlitFit` on non-16:9 displays, but the
  overlay scale uses only height and ignores the letterbox rectangle. The UI must use the
  same `FitImage` rectangle (see "Requirements added by the user"). [V gap, A fix]

### Colour space and HDR [V by reading; not reproduced]
- SDR: `Swapchain::FindPresentFormat` accepts only `R8G8B8A8Unorm` or `B8G8R8A8Unorm`
  (`vk_swapchain.cpp` ~180). These are **UNORM, not sRGB**, so shader outputs are written
  without a gamma conversion. Hex colours taken from the movies are in the game's own
  encoded space and can be used directly as vertex colours, provided blending is also
  done in that space (ImGui does this). This is why game-derived hex tokens will match.
- HDR: when the game requests HDR output, the swapchain is recreated as
  `A2B10G10R10UnormPack32` + `Hdr10St2084` (`SetHDR`, `driver.cpp` Flip). But
  `BbOverlay::Init` builds its pipeline with the **initial** surface format and never
  rebuilds. If HDR is enabled the overlay pipeline's colour format would not match the
  attachment. HDR is gated by `EmulatorSettings.IsHdrAllowed()`; whether bbport ships
  with it enabled was not checked. **Risk, needs a spike**, plus PQ-encoding the UI
  colours (UI white at a sensible nits value) if HDR is supported at all.

### Input [V]
- Window thread: `window.cpp` calls `BbOverlay::HandleEvent(event)` for every SDL event
  (~line 260); a true return consumes it. `UpdateTextInput` toggles SDL text input;
  `SetTextEntry` shows the game's own IME dialog text as an ImGui box.
- Pad: `runtime_pad.c` `sample_host` returns a neutral pad when
  `bbgpu_overlay_captures_input()` (line ~566), and `sample` sets `hold_after_capture` and
  returns (~824). Mouse capture is released via `window.cpp` ~308. `bbgpu_overlay_captures_input`
  is defined in `gpu/shim/bbgpu.cpp:346` as `BbOverlay::CapturesInput()` =
  `menu_open || mods_open || text_entry_active`.
- This gives a clean capture/release model already. `hold_after_capture` exists to avoid
  a held button leaking into the game on close. Its exact semantics were not traced.
- Open gestures today: **Insert** or **L3+R3** open the settings overlay;
  **F1** (override `BB_MODS_KEY`) opens the Mods window; F9 is reserved
  (`runtime_input_config.c` `IsUnconditionallyReserved`: Insert and F9). The pad L3+R3
  path is in `HandleEvent` (stick buttons set `l3_down`/`r3_down`).
  The game itself uses neither combo. [A, no clash seen in the input config]

### Existing mod UI [V]
- `gpu/shim/bbport_modui.cpp` draws the Mods window and every mod panel inside
  `BbOverlay::Render` under `imgui_mutex`. It uses `modhost_try_lock`, so a reload on the
  game thread makes the overlay reuse the last draw data (replaying only if no draw
  command carries a user callback) instead of stalling.
- The ABI (`sdk/include`, `bbm_host`): `ui_panel`, `ui_text`, `ui_button`, `ui_checkbox`,
  `ui_slider_float/int`, `ui_input_text`, `ui_combo`, `ui_collapsing_header`, `ui_table_*`,
  `ui_notify`, `ui_contribute`, and the escape hatch `ui_imgui_context`. Widgets are valid
  only during a panel's draw callback.

### Pause and online [mixed]
- Bloodborne does not pause in menus. [given in the brief] The overlay does not pause
  the game: it only neutralises pad input, so the player stands still but enemies act.
  True pause would need hooking game time and is **not recommended**: the runtime is
  advertised as offline here, but saves/timers and any future multiplayer mod make a
  pause unsafe. Recommend: mod menu is non-pausing; show a "game is still running"
  hint; auto-close on damage is optional. [A]
- Multiplayer mods are allowed (brief). Toggling a mod mid-session must not occur while
  a mod holds networked state; use the dependency/ownership model in section 5/6.

## 3. Option comparison

| | A. Ruffle (SWF/AS3) | B. Native retained UI (Rust) | C. Skinned ImGui |
| --- | --- | --- | --- |
| Native fidelity | Highest by construction if GFx art loads. Unverified here. | High: same tokens, fonts, textures, measured motion. Pixel match needs work per widget. | Medium; ImGui metrics and widgets show through |
| Authoring | Flash toolchain, AS3; unfamiliar to mod authors | Rust/declarative schema; one widget kit | Immediate-mode code, very easy |
| Rust integration | Needs wgpu device/texture sharing with Vulkan; ExternalInterface bridge | Direct, same process and queue, thread model already understood | Existing |
| Performance | Offscreen render + composite; second GPU API [A] | Light; a few quads and text | Light |
| Licence | Apache-2.0 OR MIT [V README]; Java build step [V] | Own code | MIT (already shipped) |
| Maintenance | Tracks upstream; AS3 "pretty well, not finished" [V README] | Own code, bounded scope | Low |
| Hot reload | Reload SWF easily | Reload schema; widgets are data | Trivial |
| Risk | GFx tags (`DefineExternalImage2`, exporter tags) not standard SWF; Ruffle likely rejects or ignores them [A, not confirmed in any source found]. Needs a preprocessing step per movie. | Time to reach look parity | Never looks native |

**Ruffle details:** embedding as a library, sharing a `wgpu` device with an existing
Vulkan renderer, and input-injection API shape were **not verified**: the README says
nothing about library use, and a docs.rs lookup for `ruffle_render_wgpu` returned 404.
Statements in the brief about those points remain assumptions. The only verified AS3
statement is the README's: "supports ActionScript 1, 2 and 3 pretty well, but it's
still not finished by any means." Vanilla Bloodborne AS3 is timeline control only
(`FadeIn` -> `Loop` -> `FadeOut`) [V from labels], which Ruffle would very likely run,
but that was not tested.

**Recommendation:** B, with C as the interim layer and developer fallback. Revisit A
only if a one-day spike shows Ruffle loading preprocessed `optionsetting.gfx` with
external images and compositing it into the Vulkan frame.

## 4. Asset and style pipeline

### Verified facts (private survey `.local/ui-research/gfx-survey-20261022T075934314268Z`)
- 103 menu movies surveyed here: 102 are 38400x21600 twips = **1920x1080**, 30 fps; the
  only exception is `font.gfx` (550x400, 24 fps, but it is a font library). [V]
- All 861 text fields reference one font class, **`MenuFont_01`**, defined in
  `menu/<lang>/font.gfx` (e.g. `enggb/font.gfx`: a `DefineFont` in a 4.5 MB movie, symbol
  `MenuFont_01`). Movies import `font.swf` rather than embedding. [V]
  The font is per-language (dandk, deude, enggb, engus, finfi, frafr, itait, jpnjp, korkr...
  directories exist). Decoding glyph outlines to a runtime atlas is feasible; not tested. [A]
- **Text colours**, by field count: `#a5a58c` (231), `#a0a08c` (223), `#cccccc` (111),
  `#a6a397` (66), `#a07f4c` (42), `#a0824b` (32), `#ffffff` (15), `#999999` (14).
  Also seen in `optionsetting`: `#bdd6d6`, `#505050`, `#d02f2f`. [V]
  The brief's `#a0895f` did **not** appear in the top set; do not assume it.
- **Sizes**: 24 (412 fields), 22 (357), 28 (24), 20, 16, 14; one 124 outlier. Bold,
  centre alignment and letterSpacing (e.g. 2.0 on titles) are set in HTML per field. [V]
- **Motion**: label sets `FadeIn` / `Loop` / `FadeOut`. FadeIn-to-Loop spans 5 frames
  in 44 timelines, 6 in 27, 15 in 8, 9 in 5, 7 in 4 (at 30 fps: 167-200 ms for the common
  cases). The brief's "about 6 frames" is the common case, not universal. Cursor
  animation length (30 frames) was **not** measured here. [V partially]
- **Textures**: movies reference external images by export name (`KG_L_UDLR`,
  `KG_L_LR`, `KG_Cancel`, `KG_OK`, `MENU_Common_00090`, `OptionCommand_Texture` ...), with
  sizes (32x32 prompts, 1024x1024 sheets). `bitmap_format` 13. Textures live in
  `menu/common.tpf.dcx` and similar (`dvdroot_ps4/menu/*.tpf.dcx`). [V names/sizes]

### Not verified
- Decoding TPF: no TPF/DCX decoder exists in `src/scripts` (only a "retained at source"
  note). A decoder (DCX decompress, TPF parse, PS4 detiling, BC format decode) must be
  written or sourced. Read from the user's install at runtime; never redistribute. [A]
- Menu sound effects: the game has FMOD `.fev/.fsb` banks in `dvdroot_ps4/sound`. Which bank holds
  menu cues, and whether the runtime can call the game's own sound playback, was **not**
  investigated. Recommended order: (1) trigger the game's own cue via its sound API if
  one is reachable; (2) otherwise decode from the user's FSB at runtime. [A]
- Localisation: menu text lives in FMG files under `msg/`. Not examined. Mod strings
  should be mod-owned with an optional fallback; the font file must be chosen by locale. [A]

### Style tokens (draft, for the retained UI)
Primary text `#a5a58c` / `#a0a08c`, secondary `#a6a397`, focused/accent `#a0824b`/`#a07f4c`,
bright `#cccccc`/`#ffffff`, disabled `#505050`, error `#d02f2f`. Title 24 bold with
letterSpacing 2; body 22; small 16-20. Fade 5-6 frames at 30 fps. Row/panel/cursor
geometry has **not** been extracted yet: take it from `named_tree` placement data of the
chosen reference movie.

### Which screen to copy
The mod list is a vertical list with right-side detail. The verified native
`itemlist_weapon` (seven rows, `ScrollBarV`, per-row name/icon/count/selection fields) plus
`itemdetailtext` is the closest match; `optionsetting` fits per-mod settings (rows with
combo/dropdown). Recommend list/detail like the inventory for the mod list and
`optionsetting`-style rows for settings. [V structure from `gfx-survey.md`]

## 5. Mod menu UX and settings schema

### Main view
List of mods: name, version, author, enabled state, status (Active, Loading, Failed with
reason, Needs restart). Detail pane: description, dependencies, last error, settings entry.
Navigation: D-pad/stick move, Confirm toggles or opens, Back leaves, L1/R1 switch tabs
(Mods, Settings), Left/Right is also accepted (observed on 22 October: the **stock
inventory responds to Left/Right and not L1/R1** on this port; follow the same).

### Load, unload, dependencies
Toggle calls the existing host load/unload/reload. UI shows pending then result. If
other mods depend on the target: block disabling while dependents are active, offering
"disable dependents too" as an explicit cascade with a list; warn (not block) on soft
references. The host already tracks `MODHOST_ACTIVE` states and per-owner reload. [V in test]

### Self-protection
The manager is a mod, but treat the manager UI as part of the host: it **cannot be
unloaded from itself**. Recommended: ship it as a built-in host panel with the mod
interface, not an ordinary unloadable mod; its row shows "Core" and the toggle is
disabled. [A, design decision]

### Settings schema in the manifest (proposal)
```toml
[mod]
id = "example.my-mod"
name = "My mod"
version = "1.2.0"
author = "Someone"
abi = "1.10"
dll = "my_mod.dll"

[[settings]]
key = "enabled_hud"
type = "bool"
label = "Show HUD"
default = true
apply = "live"            # live | restart

[[settings]]
key = "damage_scale"
type = "float"            # int | float
label = "Damage scale"
min = 0.5
max = 2.0
step = 0.05
default = 1.0
apply = "live"

[[settings]]
key = "mode"
type = "enum"
label = "Mode"
choices = ["Normal", "Hard", "Chaos"]
default = "Normal"
apply = "restart"

[[settings]]
key = "toggle_key"
type = "keybind"
label = "Toggle key"
default = "F4"
apply = "live"

[[settings]]
key = "title"
type = "string"
label = "Title"
max_len = 32
default = ""
apply = "live"
```
Validation is done by the host from the schema before the mod sees a value. Persist
per mod in `%APPDATA%`-style user data next to saves (`user/mods/<id>.settings.toml`),
written atomically on change, with the schema version recorded for migration. Existing
mod state persistence (`save_state`/`restore_state`) is for runtime state, not settings. [A]

## 6. Mod-facing API sketch and unload safety

```rust
// Schema-first, with callbacks only for change notification.
impl Mod for MyMod {
    fn load(cx: &mut Cx) -> Result<Self> {
        cx.settings_subscribe()?;                  // schema comes from the manifest
        Ok(Self { damage: cx.setting_f32("damage_scale")? })
    }
    fn event(&mut self, cx: &mut Cx, ev: &Event) {
        if let Some(c) = ev.setting_changed() {    // delivered on the game thread
            if c.key == "damage_scale" { self.damage = c.f32(); }
        }
    }
}
```
- **Schema-first** is preferred: the manager can show, validate, search and persist
  settings without running mod code, and an unloaded mod's settings remain editable.
  Code callbacks (`ui_panel`) stay for custom screens.
- **Threading:** UI runs on the present thread; mods run on the game thread. UI never calls
  mod code. A setting edit is validated in the UI, then queued as an event and delivered
  through the existing event path (as native-menu events are today). [V pattern]
- **Unload safety / ownership:** the UI holds only host-owned copies (schema, current
  value, display strings) keyed by `(owner id, generation, key)`. Any action carries
  the generation it was drawn from; the host drops it if the owner has been reloaded or
  unloaded. The overlay must never retain a function pointer into a mod DLL. The existing
  Render path already refuses to replay draw data containing user callbacks while a
  reload may be running. [V]
- **Fuller custom screens later:** keep `ui_panel` (immediate) and add a retained node
  tree (the `ui-components` design in `architecture.md`) rendered by the same layer.

## 7. Risks and open questions
1. HDR pipeline-format mismatch (section 2). Highest-priority spike.
2. Letterbox mapping of the 1920x1080 canvas on non-16:9 displays.
3. TPF/DCX decoding, PS4 detiling, and font outline extraction are all unbuilt.
4. Menu audio route unknown.
5. Pad open gesture L3+R3 and `hold_after_capture` semantics need a live check on close
   (button-leak risk), including keyboard/pad device switching while open.
6. Ruffle: library embedding, wgpu device sharing and GFx tag handling unverified.
7. The `Kind::Screen` native-menu route and this overlay route are different UIs; decide
   which owns the mod menu long-term (recommendation: the overlay, section 1).
8. Pause: not recommended. Online: toggling mods mid-session needs per-mod "safe to
   unload" flags.
9. The brief's colour `#a0895f` and "6-frame fade" are not universal in the data.

## 8. Proposed first milestone (about 8 working days)
1. **Day 1.** Spike ladder 1-2: draw one rectangle through a stub renderer using the
   existing hook; confirm capture/release including a held button on close. Test with
   HDR on and off.
2. **Day 2-3.** TPF/DCX reader and BC decode to an atlas from the user's install;
   render one real prompt icon (`KG_OK`) beside a screenshot of the vanilla screen.
2b. **Day 3.** 60 Hz UI clock and fit-rectangle canvas: animate a 6-frame (30 fps
   authored) fade, check identical timing at 60/120 Hz presents and smooth vs stepped playback and correct placement at 16:9, 21:9 and
   4:3 window sizes.
3. **Day 3-4.** Font: extract `MenuFont_01` outlines, render "Mods" at size 24, colour
   `#a5a58c`, side-by-side screenshot with `optionsetting`.
4. **Day 5-6.** Static mock of the mod list (list, scrollbar, detail, key guide) at 1080p
   and a letterboxed ratio.
5. **Day 7-8.** Wire the manifest schema, one bool and one enum setting, toggle with
   generation-checked actions, and unload/reload tests. Keep ImGui as the fallback.
