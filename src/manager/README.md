# Mod Manager (`bbmod.manager`)

The in-game manager, formerly the Mod Menu. Open it with F2, the pad combo, or the pause menu's Mods entry.

- **Mods**: loaded state, load/unload/reload.
- **Settings**: each mod's declared settings.
- **Browse**: mod lists and installs. Add a source as `owner/repo` (reads that repository's `modlist.json`),
  a GitHub link, or an `https://` link to a JSON file. Selecting a mod reads its `mod.toml` from its repository
  and checks the ABI against this loader. Install, Update and Remove work on the mod's GitHub Release ZIP
  (verified against a `<asset>.sha256` file or the list's `sha256`), unloading a running mod first and loading it
  again afterwards. The previous version is kept in `mod-data/bbmod.manager/previous`.

The official list is `DigitalEthosGlobalGaming/bb-modding-mod-list`. Mods run native code in the game process
and lists are not vetted. Mods with `manage = true` cannot be installed from a list.

Modules: `catalog` (lists, manifests, releases, compatibility), `install` (safe ZIP install and removal),
`jobs` (network and disk work on a host-managed thread), `browse` (state of the Browse tab), `model`/`view`
(widgets), `lib` (host wiring). Build with `cargo build --release`; `cargo test --release -- --ignored`
also runs the live network check.
