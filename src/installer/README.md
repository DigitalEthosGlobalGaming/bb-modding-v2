# bb-modding installer

Builds `bb-modding-installer.exe`: a Windows GUI installer that asks for the game folder
and the bbport folder, shows what will change, then installs the patched mod loader and the
Mod Manager. Design and rationale: [docs/INSTALLER-AND-MOD-MANAGER.md](../../docs/INSTALLER-AND-MOD-MANAGER.md).

## Use

The installer carries a patched runtime for each supported bbport version and picks the one that matches the player's
`bb-probe.exe` (by SHA-256). An unknown version is refused with nothing changed (`--runtime <id>` forces one; use with care).
Installing again undoes the previous install first, so backups always hold the player's own bbport files and an
updated bbport is never overlaid with an older build. `--detect --port <dir>` prints the detected version.

Run the exe and follow the pages: welcome, folders (validated live: game `CUSA03173` update 1.09,
bbport with `Bloodborne.exe` and `bb-probe.exe`), review (add / replace / same per file), install.
It refuses to run while the game or launcher is open. Scripted use:

```powershell
bb-modding-installer.exe --game <game folder> --port <bbport folder>   # install, no window
bb-modding-installer.exe --detect --port <bbport folder>               # which bbport version is installed
bb-modding-installer.exe --uninstall --port <bbport folder>            # undo the last install
```

Every replaced file is copied to `<port>/bb-modding-backup/<time>/` and its before/after hashes are
recorded in `<port>/bb-modding-install.json`. Uninstall restores them, and refuses to touch a file that
changed after the install. Saves, `user/`, `config/`, `mod-data/` and `bbport.ini` can never be part of
a payload. The installer is unsigned, so Windows SmartScreen warns on first run.

## bbport versions

The mod loader is compiled into `bb-probe.exe`, so the installer carries one patched runtime per supported
bbport release (`runtimes` in `src/scripts/tools.lock.json`) and picks by the SHA-256 of the installed stock
`bb-probe.exe`. An unrecognised version is refused and nothing is changed. Reinstalling first restores the original
files, so builds never stack. To add a release: a patch under `src/integration/`, an entry with its stock hash in
`tools.lock.json`, `bb.ps1 build-runtime -Runtime <id>`, then rebuild the installer.

## Build

```powershell
.\build.ps1 -Test            # manager, payload, tests, then dist/bb-modding-installer.exe (+ .sha256)
.\build.ps1 -SkipRuntime     # no staged runtime: the payload holds only the Mod Manager
```

`payload/build-payload.ps1` packs every staged runtime (`.local/runtime-stage/<id>/bin`, from `bb.ps1 build-runtime -Runtime <id>`)
and the built manager into `payload/payload.zip` (ignored), which `build.rs` embeds. Without a payload the crate
still builds, but the installer reports that it has nothing to install. Nothing is deployed to a game or port.

`../../.github/workflows/installer.yml` builds it on push and pull request and uploads it as the
`bb-modding-installer` artifact; a `v*` tag also attaches it, with its checksum, to a GitHub Release.
The runtime is built with MSYS2 and cached on its inputs. That workflow has not been run on GitHub yet.

## Layout

| Path | Contents |
| --- | --- |
| `src/validate.rs` | Game (PARAM.SFO) and port checks |
| `src/payload.rs` | Payload format and bbport version detection |
| `src/auto.rs` | Choosing the runtime, clean reinstall |
| `src/install.rs` | File plan, install with backups, uninstall |
| `src/main.rs` | Wizard (egui) and command-line mode |
| `payload/` | Payload assembly script; the built zip is ignored |
