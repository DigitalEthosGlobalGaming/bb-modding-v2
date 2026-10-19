> Historical workspace notes. Paths and validation describe the original workspace; use ../SETUP.md for this toolkit. Inspector and bulk recovered tables are excluded.

# Instructions for a bbmod mod

Copy this file to this mod's `AGENTS.md`, then add its purpose and acceptance checks.

## Workspace

- Use Windows-compatible PowerShell. Load `modding/dev-env.ps1` from the workspace.
- Read `modding/modding-docs/README.md`, `DEVELOPMENT.md`, `SHARED-HOOKS.md` and
  `TESTING.md` before changing a mod. Read the host integration and SDK documents
  before framework changes.
- Mod sources belong in `modding/mods`. Installed DLLs belong in
  `bbport-windows/bbmods/<unique-mod-id>`; compile outside that watched directory.
- Preserve packages, game assets, saves, user settings and other developers' work.

## Implementation

- Use the local Rust `bbmod` SDK and typed shared services. Inspect actual API
  definitions; do not invent functions from specifications or research notes.
- Check feature availability and handle unsupported builds without scanning the
  entire heap at startup. Never hardcode an address copied from a previous session.
- Treat player polling as state observation, not a pre-damage or attack hook.
- Keep shared native hooks in the host. Do not detour an address independently when
  a host service owns it. Respect claim ownership and conflict errors.
- Default test mods to observation. Make gameplay mutations explicit opt-in,
  bounded and visible. Preserve unrelated flags and validate current ownership.
- Use owned callbacks, panels and `cx.spawn`; promptly stop workers. No unmanaged
  threads or persistent global host pointers. Keep Rust panic unwinding enabled.
- Save logical state only across reload; rebuild subscriptions and resource handles.
- Do not claim native UI embedding from a separate ImGui panel. Clearly mark
  unimplemented native menu, equipment, inventory and action semantics.

## Build and verify

- Compile and run relevant tests before atomic deployment; retain a rollback copy.
- Preserve installed manifests and external user config. Never deploy a partially
  built DLL. Do not restart or operate the game UI without the user's instruction;
  this user performs gameplay interactions while Inspector handles diagnostics.
- Test two subscribers together, unloading/reloading each while the other remains
  active. Verify mutation cleanup and conflict behavior, including player replacement.
- Report checks actually run, distinguishing synthetic tests, read-only live checks,
  and user-confirmed gameplay behavior. Document remaining unknowns.
