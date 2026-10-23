# Bloodborne PC Modding Kit

Add mods to Bloodborne on PC. The modding kit installs a mod loader into your existing
[bbport](https://github.com/DigitalEthosGlobalGaming/bb-modding-v2) setup and an in-game
**Mod Manager**, where you browse, install, update, switch on and off, and configure mods
without leaving the game.

- **One installer.** Pick your game and bbport folders, review the changes, press Install.
- **In-game Mod Manager.** Browse mod lists, install and update mods, edit each mod's settings.
- **Safe to try.** Replaced files are backed up, saves and settings are never touched, and one click removes the kit again.

> This is an unofficial community project and is not affiliated with Sony, FromSoftware or Supermedo.
> You need your own copy of the game. No game files are included or distributed.

## Requirements

- Windows 10 or 11, 64-bit.
- Bloodborne for PC already working through bbport: a folder containing `Bloodborne.exe` that starts to the title screen.
- Your game files: the folder that contains `CUSA03173` (European edition with update 1.09).

## Install

1. Download `bb-modding-installer.exe` from the [latest release](https://github.com/DigitalEthosGlobalGaming/bb-modding-v2/releases/latest).
   Each release also has a `.sha256` file if you want to verify the download.
2. Close Bloodborne and the launcher.
3. Run the installer. The file is not code-signed, so Windows SmartScreen may show a warning. Choose **More info**, then **Run anyway**.
4. Choose your **game folder** and your **bbport folder**. Each shows a green OK once it is valid.
5. Check the list of files and press **Install**. Any file that is replaced is backed up first.
6. Start the game as you normally do.

## Using the Mod Manager

Open it with **F2**, with **L1 + R1 + Touchpad** on a controller, or from the pause menu's **Mods** entry.

| Tab | What it does |
| --- | --- |
| **Mods** | Shows the installed mods. Load or unload one, or reload it after an update. |
| **Settings** | Each mod's own settings. Under **Mod Manager** you can change the open key and add mod lists. |
| **Browse** | Lists the mods from your mod lists. Select one to see its details and whether it works with your loader, then press **Install**. |

**Installing a mod.** Open **Browse**, select a mod and press **Install**. The mod is downloaded, checked and loaded.
**Update** appears when a newer version is available. **Remove** takes the mod out; the previous copy is kept in
`mod-data/bbmod.manager/previous` inside your bbport folder.

**Adding a mod list.** The official list is included. To add another, open **Settings > Mod Manager**, choose
**Add mod list** and enter `owner/repo` (a GitHub repository that contains a `modlist.json`) or an `https://` link to a
`modlist.json` file. It then appears in **Browse**.

## Uninstall

Run the installer again, press **Next** through to the review page and choose **Uninstall instead**. This restores the
files the installer replaced and removes the ones it added. Your saves, graphics settings and the mods' settings are not touched.
If you changed an installed file yourself after installing, it is left alone and the uninstall tells you which one.

## Troubleshooting

| Problem | What to try |
| --- | --- |
| The installer says to close the game | Close Bloodborne, the launcher and any `bb-probe` process, then try again. |
| A folder is not accepted | The game folder must contain `CUSA03173` with update 1.09. The bbport folder must contain `Bloodborne.exe` and `bin\bb-probe.exe`. |
| The Mod Manager does not open | Check that the installer finished, then look at `bbmods.log` in your bbport folder. |
| A mod says it needs a newer mod loader | Download the newest installer and run it again. It upgrades in place. |
| A list shows no mods or an error | Check your internet connection and that the repository or link is spelled correctly. |
| The game stops starting | Run the installer's uninstall to go back to your previous files, then report the problem with `bbmods.log`. |

## Staying safe

Mods are programs that run inside the game. Mod lists are not vetted, and a mod appearing in a list is not an
endorsement. Only install mods from people you trust. The Mod Manager downloads over HTTPS, checks files against
published checksums when a mod provides one, and refuses archives with unsafe paths.

## For developers

Building the kit, writing mods and publishing a mod list are covered in [DEVELOPERS.md](DEVELOPERS.md).
The design of the installer and Mod Manager is in [docs/INSTALLER-AND-MOD-MANAGER.md](docs/INSTALLER-AND-MOD-MANAGER.md).

## Licence

See [LICENSING.md](LICENSING.md). Third-party notices are kept with the files they apply to.
