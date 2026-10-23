# Installer and Mod Manager design

Status, 2026-10-23: first implementation built in `src/installer` and `src/manager`. Unit, host-integration and fake-port install tests pass; nothing has been played in-game, and the CI workflow has not run on GitHub yet.

## Goal

A player with Bloodborne on PC (bbport) runs one installer from this repo. It
installs the core mod loader and the mod manager. From then on, the player adds
and installs mods from inside the game through the mod manager, with no
development tools and no manual file copying.

Three deliverables, all in this repository:

| Deliverable | What it is | State |
| --- | --- | --- |
| Installer | User-friendly setup program that finds the game and port, installs the loader and the mod manager | New |
| Core mod loader | Framework host plus the pinned bbport integration (`src/host`, `src/integration`) | Exists; built and deployed by developer scripts |
| Mod manager | In-game mod that lists, installs, enables and configures mods | Renamed to `bbmod.manager` and extended with a Browse tab (`src/manager`) |

Mods themselves stay out of this repo, each in its own repository, and are
discovered through a mod list (see below).

## Current state

- `setup.ps1` already asks for two folders with pickers: the game parent folder
  (contains `CUSA03173/uroot`) and the bbport folder (contains `Bloodborne.exe`).
  It is a developer tool: it also extracts about 52 GB of reference data.
- `bb.ps1 build-runtime` builds the pinned Supermedo/bloodborne_pc checkout plus
  `src/integration/bbport-modhost.patch` and stages it under `.local/runtime-stage`.
  `bb.ps1 deploy-runtime` copies it into the port with a hash-recorded backup and a
  `rollback` command.
- Installed mods live in `<bbport>/bbmods/<mod id>/` with a `mod.toml` and a DLL.
- `bbmod.menu` (then in `mods/bbmod.menu`, ABI 1.11, `manage = true`) listed installed mods,
  toggles them and edits their settings. It sits in the git-ignored `mods/` folder,
  so it is not in version control or in the mod list.
- Adaptive Triggers and Difficulty are separate repositories under
  `DigitalEthosGlobalGaming`. `bb-modding-mod-list` holds a first `modlist.json`.

## Installer

### User flow

1. Welcome screen: what will be installed and what will not be touched.
2. Pick the **game folder** (contains `CUSA03173/uroot`). Validate the title ID and
   update 1.09, the same checks `setup.ps1` already makes.
3. Pick the **bbport folder** (contains `Bloodborne.exe`), or choose "install a new
   port here" and pick an empty target. Validate the files.
4. Review screen: shows exactly which files will be added or replaced and where the
   backup goes.
5. Install, with progress. Then offer to launch the game.

Defaults are remembered, and both folders can be given on the command line
(`--game`, `--port`, `--quiet`) for unattended use, mirroring `setup.ps1`'s
`-NonInteractive`.

### What it installs

1. The core loader into the bbport folder: the patched runtime binaries, the host
   and the `bbmods/` directory.
2. The mod manager into `bbmods/bbmod.manager/` (final ID to be decided).
3. A default mod-list source for the manager (the official `bb-modding-mod-list`).

It does not copy game data, touch `user/` saves or `bbport.ini`, or install any
third-party mod.

### Safety

Reuse the existing deploy model: back up every replaced file with old and new
hashes, print a rollback, and refuse to roll back files changed since install.
Add an "Uninstall" entry that runs the rollback. Installing over an existing
install upgrades in place and keeps `mod.toml` settings.

### Build and release

A GitHub Actions workflow builds the installer and uploads it as a downloadable
artifact. It bundles the prebuilt runtime stage (CI runs `build-runtime`) and the
built mod manager, so players need no MSYS2, Rust or Visual Studio.

- Every push to `main` and every pull request uploads the installer as a workflow
  artifact (`bb-modding-installer`), so anyone can download a current build from the
  run's page. Workflow artifacts expire (90 days by default).
- A version tag (for example `v0.1.0`) also attaches the same installer and a
  SHA-256 checksum file to a GitHub Release, which is the stable download link
  for players.
- The existing publication audit and the tests must pass before the artifact is
  uploaded.

### Technology

The deliverable is a single Windows executable, `bb-modding-installer.exe`, not a
PowerShell script. Players double-click it; no PowerShell, Rust or other tools are
needed. `setup.ps1` stays as the developer-only reference setup.

Decision: a small Rust GUI executable in `src/installer/`, with native
folder dialogs and the runtime stage and mod manager embedded or bundled beside it.
The repo already requires Rust, and the validation logic can be shared with the
manager. The alternative is an Inno Setup `.exe`, which is quicker to build and gives
a standard Add/Remove Programs uninstaller, but keeps validation logic in a second
language.

An unsigned `.exe` triggers a Windows SmartScreen warning on first run. Code signing
is out of scope for the first release; the release notes should say so and publish
the SHA-256 checksum.

## Mod manager

### Rename

`bbmod.menu` / "Mod Menu" becomes the mod manager. Pick the final name and ID
before the first release, because the ID is the install folder and appears in
the mod list. Suggested: ID `bbmod.manager`, display name "Mod Manager". Move it
from the ignored `mods/` folder into tracked source (for example `src/manager/`)
so it builds with the repo and ships in the installer.

### What it does at runtime

1. **Sources.** The player adds a source: a git repository URL that contains a
   `modlist.json`, or a direct URL to a `modlist.json` file. The official list is
   preconfigured. Sources are stored in the manager's settings.
2. **Browse.** The manager fetches each source and shows its mods (name,
   description, author).
3. **Details.** Selecting a mod fetches that mod's own manifest, the `mod.toml`
   at the root of its repository at the listed `ref`. The manifest supplies the
   SDK/ABI version, the DLL name, version and settings, so the list stays small.
4. **Compatibility check.** Compare the mod's `abi` with the host's ABI and
   capabilities. Show "compatible", "needs newer loader" or "unsupported", and
   block install when incompatible.
5. **Install.** Download the release asset, verify its checksum, extract to
   `bbmods/<id>/`, and load it with the existing reload mechanism. Keep the
   previous version for rollback.
6. **Manage.** Enable, disable, update and remove, plus the existing per-mod
   settings editor.

### Distribution of mod binaries

The manager installs prebuilt binaries; it does not build from source. Each mod
repository publishes a GitHub Release asset (a ZIP of the `bbmod.<id>/` folder)
built by that repository's CI. Adaptive Triggers already builds a ZIP in CI. The
manager resolves the asset from the release at the listed `ref` (a tag is
preferred).

## Formats

### modlist.json (exists, version 1)

Current schema is `modlist.schema.json` in `bb-modding-mod-list`.

```json
{
  "name": "bb-modding official mod list",
  "version": 1,
  "mods": [
    {
      "id": "me.difficulty",
      "name": "Difficulty",
      "repo": "https://github.com/DigitalEthosGlobalGaming/bb-modding-difficulty",
      "ref": "main",
      "abi": "1.1"
    }
  ]
}
```

Proposed additions: `"release"` (asset name or pattern), and an optional
`"mod_toml"` path for repos that do not keep it at the root. A direct-file source
uses the same JSON, hosted anywhere.

### mod.toml (exists)

The manager reads `[mod]` (`id`, `name`, `version`, `abi`, `dll`, `author`,
`description`), `[load]` and `[settings]`. Proposed additions for the manager:
a minimum loader version, and a `[requires]` table for mod dependencies. The list
entry's `abi` is a hint only; the repository's `mod.toml` is authoritative.

## Trust and security

- Mods are native DLLs that run inside the game process. Installing one is running
  arbitrary code, so the manager must say so plainly before the first install and
  before any install from a non-official source.
- The official list does not vet listed mods. Keep that disclaimer in the list's
  README and show it in the manager.
- Pin versions: the list's `ref` should be a tag or commit, and the installer
  checks the downloaded asset against a SHA-256 published in the release or list.
- Network access happens only on user action (add source, browse, install).
  Downloads are HTTPS only, and extraction rejects paths outside `bbmods/<id>/`.
- The manager never runs a mod's code before the user enables it.

## Repository layout (proposed)

| Path | Contents |
| --- | --- |
| `src/installer/` | Installer program and its tests |
| `src/manager/` | Mod manager mod (moved from `mods/bbmod.menu`, ID `bbmod.manager`) |
| `src/host/`, `src/integration/`, `sdk/` | Unchanged core |
| `docs/INSTALLER-AND-MOD-MANAGER.md` | This document |

Other repositories (all under `DigitalEthosGlobalGaming`): `bb-modding-mod-list`,
`bb-modding-difficulty`, `bb-modding-adaptive-triggers`.

## Milestones

1. Rename the Menu to the manager and move it into tracked source; builds in CI.
2. Installer MVP: pickers, validation, install, backup, uninstall. Manual test on a
   clean port and on the current `bbport-windows`.
3. Manager: sources and browse, reading `modlist.json` from a git URL or file.
4. Manager: fetch `mod.toml`, compatibility check, install, update, remove.
5. Release automation: a GitHub Actions workflow builds the installer as a downloadable
   artifact on every push, and attaches it with checksums to a Release on version tags.
6. Update `modlist.json` with release assets and checksums, and write the mod
   author guide (how to publish a mod so it can be listed).

Gameplay validation is a separate step for each milestone. Synthetic tests and a
title-screen check do not establish in-game behavior.

## Open questions

- Final manager name and mod ID.
- Installer technology: Rust executable (recommended) or Inno Setup; both give an `.exe`.
- Code signing for the installer, to avoid the SmartScreen warning.
- May a prebuilt bbport runtime be redistributed? Check the upstream
  Supermedo/bloodborne_pc license and its terms before shipping binaries in the
  installer; otherwise the installer would have to build or fetch them.
- Is `bb-modding-v2` public? Mods pin their SDK from it (Difficulty already does),
  and the manager's checks rely on a published SDK.
- Should the manager update itself, and how does it replace a DLL that is loaded?
- Which dependency model do mods need (none at first, or `[requires]`)?
- Licenses for the mod repositories; none of the new repos has one yet.
