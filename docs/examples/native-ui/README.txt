Native Menu Example
===================
An ABI 1.8 native menu example using shared framework services.

It registers a settings screen under Mods, a System shortcut, a nested screen,
a logging toggle and an action that adds/removes a row. It logs native creation,
sampled root open/close, cursor and owned action/value events. Changes to an
open list appear when it is reopened. No ImGui fallback is used.

Build the example from the toolkit root:
  cargo build -p native-ui-example --release
Run src/scripts/test.ps1 -Native for the toolkit host suite.

The release DLL/manifest are staged in dist/example.native-ui. This changes no
game installation. Without the native adapter, it logs that definitions were
registered but rendering is unavailable. Use the separately rebuilt compatible
runtime for eventual playtesting; the old installed host may reject the example.

BB_MODS_NATIVE_UI=1 opts into the experimental adapter. BB_NATIVE_UI_ASSETS points
to .local/native-ui-assets, containing both the fourth top cell and the
stock optionsetting.gfx list template. The template is required; without the
top-cell override use System -> Mods. The framework retains native callbacks/strings so
unloading this DLL leaves no callable pointer into it in the game.

Actual engine rendering/navigation/back behavior has not been verified. See
docs/RUNTIME.md and docs/VALIDATION.md for scope, commands, tests and limitations.
