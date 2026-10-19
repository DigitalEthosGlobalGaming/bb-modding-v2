# Shared UI examples

This mod demonstrates two shared overlay locations: `bbmod.dashboard` and
`bbmod.settings`. Inspector and Adaptive Triggers also contribute to these slots
when the ABI 1.4 shared UI backend is available. This is the host's ImGui overlay,
not native Bloodborne menu injection.

The dashboard includes a counter and a toggle that releases/recreates only this
mod's settings contribution. Settings includes a separate counter, checkbox,
slider, selection and text input. Both counters deliberately use the same button
label (`Increment`) to verify widget ID isolation. All controls affect example
state only; this mod writes no game memory or controller output.

State survives hot reload, including whether settings are hidden. Host resource IDs
are never saved. Unloading removes this mod's contributions while other mods in
the same slot remain. Corrupt or incompatible saved state resets to defaults.

```powershell
# From the workspace root; builds debug and release, runs tests, packages only:
.\modding\bloodborne_pc\third_party\bbmod\examples\ui-examples\build.ps1 -Test
```

Output package: `dist/example.ui-examples`. The workspace release deployer handles
installation and rollback; this script never deploys. Source manifest ID is
`example.ui-examples`, DLL `ui_examples.dll`, ABI `1.4`.

Offline validation includes two Rust tests for complete versioned state transfer
and malformed-state rejection, plus a native harness loading the actual UI DLL
and a second actual mod. It checks independent same-label counters, another mod's
content surviving release/unload, and 20 hide/show/reload cycles without losing
state or accumulating active contributions. Released Rust callback allocations are
retained until mod destruction; this demo is not a pattern for unbounded registration
loops. The harness uses fake UI widgets; it does
not establish visual quality or mouse/controller behavior in the running game.
The workspace `modding/test-shared-ui-mods.ps1` additionally loads the actual
Inspector, Adaptive Triggers and UI Examples DLLs together to verify all six
sections use the shared backend and remain isolated across reload/unload.

## In-game checklist

1. Open F1 and locate `bbmod.dashboard` and `bbmod.settings`.
2. Verify UI Examples, Inspector and Adaptive Triggers share the locations.
3. Click each example `Increment`: only its own counter should change.
4. Change the slider, style and label; dashboard should reflect them.
5. Hide example settings: Inspector/Adaptive contributions must remain usable.
6. Reload UI Examples: values and hidden state should survive; show settings again.
7. Unload UI Examples: both its sections disappear, other contributions remain.
8. Reload Inspector/Adaptive individually: other sections should remain stable.

This checklist is pending user validation; offline harness success is not a claim
that these UI interactions were exercised in the game. See the workspace
[return-to-game checklist](../../../../../TEST-WHEN-BACK.txt).
