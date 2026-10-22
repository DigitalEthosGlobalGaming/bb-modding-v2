# UI-Examples-Mod

This ABI 1.10 mod contributes native dashboard and settings screens under
**Mods → UI-Examples-Mod**. It registers no ImGui panel. These screens reuse
the game's list template and need the live adapter for open-screen feedback
and paging. This is the working menu baseline for the
[component plan](../../ui-components/README.md). Full-screen layouts, shoulder
tabs, inventory views and HUD mounting are not implemented yet.

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
# From the bb-modding toolkit root: build, test and package only
.\bb.ps1 build-mod docs/examples/ui-examples --test
```

Package: `dist/example.ui-examples`, DLL `ui_examples.dll`. The script never
installs it. Its actual DLL harness supplies no ImGui backend and checks all
controls, Unicode, duplicate-label counters, hide during editing, partial
registration failure/retry, stale actions and twenty visible plus twenty hidden
reloads. The workspace native runner also checks ownership, text leases, game
IME handlers, paging and registry capacity as separate checks. The package record
reports only the tests run by this build command. These checks do not prove
rendering or navigation in the game. The stock Top command list has five slots;
the live adapter uses three content rows plus up to two paging actions.

## In-game check

1. Open Mods → UI-Examples-Mod and navigate every dashboard/settings page.
2. Increment each screen's counter independently.
3. Change Enabled, Level, Style and Label; check dashboard values and Back.
4. Hide/show settings and confirm accepted values remain.
5. Reload with settings visible and hidden; check counters and settings.
6. Unload and check other mods remain available.

Prepare a separate trial after building the runtime and this package:

```powershell
python src/scripts/prepare_ui_trial.py
```

Run the generated `launch.ps1` from the printed private trial directory. It uses
copied settings and saves, separate caches/logs, offline mode and the staged
runtime. Close any existing game/launcher first. Original installation files are
not deployed or replaced. On 21 October 2026 the user confirmed repeated paging,
including quick changes, keeps rows visible and bright in the isolated trial.
Trace evidence also showed restored rows at full alpha and their settled positions.
The adapter waits for the native entrance fade before changing the row count;
hiding slots mid-fade had stranded their transforms. The System heading remains
an open issue. This paging check does not validate the full planned gallery or
every input device. Use `--trace` for bounded native slot diagnostics if needed.
