# Shared UI examples

This ABI 1.10 mod contributes native dashboard and settings screens under
**Mods → Shared UI Examples**. It registers no ImGui panel. These screens reuse
the game's list template and need the live adapter for open-screen feedback
and paging.

The dashboard has its own counter, accepted label and a toggle that removes or
restores only this mod's settings. Settings has a separate counter, Enabled
toggle, bounded integer, style alternatives and a system text editor. Both
counter actions deliberately say **Increment**, with separate stable IDs.
Controls change example state only; no game memory or controller output is
written. Style is shown by name; native text retains the game's stock styling
rather than the old overlay's custom RGB tint.

Hot reload retains the original UIEX version-one state format, including hidden
settings. Resource IDs and pending edits are never saved. Hide/unload cancels an
owned editor; accepted values survive hide/show and reload. A failed show rolls
back partial registration and the optimistic native toggle, leaving a retryable
dashboard. Other mods' controls remain owned independently. Typing needs a
keyboard; Enter/Cross accepts and Escape/Circle cancels.

```powershell
# From the workspace root: build, test and package only
.\modding\bloodborne_pc\third_party\bbmod\examples\ui-examples\build.ps1 -Test
```

Package: `dist/example.ui-examples`, DLL `ui_examples.dll`. The script never
installs it. Its actual DLL harness supplies no ImGui backend and checks all
controls, Unicode, duplicate-label counters, hide during editing, partial
registration failure/retry, stale actions and twenty visible plus twenty hidden
reloads. The workspace native runner also checks ownership, text leases, game
IME handlers, paging and registry capacity. Eight native DLLs coexist beyond the
old 128-node budget. The mixed-backend test keeps the pending Inspector/Adaptive
controls reachable during migration. These checks do not prove rendering or
navigation of this new screen in the game.

## In-game check

1. Open Mods → Shared UI Examples and navigate both dashboard pages.
2. Increment each screen's counter independently.
3. Change Enabled, Level, Style and Label; check dashboard values and Back.
4. Hide/show settings and confirm accepted values remain.
5. Reload with settings visible and hidden; check counters and settings.
6. Unload and check other mods remain available.
