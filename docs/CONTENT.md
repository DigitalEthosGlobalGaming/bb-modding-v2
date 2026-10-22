# Content mods

Content tools read the selected game archives and write separate overlays. They use
locally downloaded Smithbox PARAMDEF XML, not the excluded Inspector/Openborne schemas.
Run setup first so Smithbox and the local item catalog are available.

```powershell
python src/scripts/content.py extract
python src/scripts/content.py build --manifest docs/examples/content-balance/content.json
python src/scripts/content.py verify
```

The default content workspace is ignored `.local/content-workspace`. New-item allocation
IDs persist in `item-ids.lock.json`; preserve it with the mod project's private build
state. Builds cache their source/schema hashes and verify reconstructed archives before
recording the current build. Outputs contain game-derived data and must remain private.

The balance example multiplies the exact named base item's physical attack values by
1.1, including explicit upgrade rows. It is an offline example, not a gameplay-validated
balance recommendation. Author original descriptions/artwork for distributable mods;
do not commit whole modified stock archives to this toolkit.

Smithbox handles editing its supported native formats through its own project workflow.
The toolkit reference extractor expands DCX/BND4; it leaves paired archives and opaque
assets at source. Decompiled Lua is inspection output, not proof of round-trip recompilation.

Retained item examples remain in the separate workspace projects. From the toolkit:

```powershell
.\bb.ps1 build-mod ../modding/mods/item-examples --test
```

The build helper uses this toolkit's compiler, SDK and `.local/content-workspace`.
Existing allocation IDs were retained during cleanup. Preserve this workspace's
`item-ids.lock.json`; deleting it risks assigning new IDs to already saved items.
Native menu overlays can be generated separately with `bb.ps1 native-assets`.
Its default output is `.local/native-ui-assets`; stock menu files are read only.
`src/scripts/hud_layout.py` retains its upstream GPL-2.0-or-later notice and can
produce an explicit ultrawide overlay with `GAME --out OUTPUT --canvas-width WIDTH`.
Generated overlays are private game-derived outputs and have not been gameplay validated.
