# Setup

Requires 64-bit Windows, PowerShell, Python 3.11+, Git, .NET SDK 9 and Rust MSVC.
Rust DLL linking requires Visual Studio C++ Build Tools and a Windows SDK.
MSYS2 CLANG64 is needed only for the C host or full port builds.

Interactive setup: `./setup.ps1`. Tool downloads are pinned in
`src/scripts/tools.lock.json`; Smithbox, source archives and the NuGet dependency
are SHA-256 checked before use. Upstream tool licences stay in their downloaded trees.
The decompiler selects .NET 9.0.305 with SDK feature-band roll-forward.

For an agent or terminal-only installation:

```powershell
.\setup.ps1 -GameDirectory 'D:\Bloodborne\game' `
  -RuntimeDirectory 'D:\Bloodborne\bbport-windows' -NonInteractive
```

`-InstallPrerequisites` offers winget installs for missing prerequisites. Combining it
with `-NonInteractive` authorizes those installers without per-package questions.
Existing command names are detected; `doctor` reports their paths. If an installed
version is too old, upgrade it using the relevant official installer and rerun setup.
Python 3.11 is the tested interpreter. No administrator session is needed for the
toolkit itself; system installers may request elevation.

Options:

| Option | Effect |
| --- | --- |
| `-ConfigureOnly` | Validate and save ignored machine paths; no tool downloads/extraction |
| `-Mode scripts` | Extract script/action/AI archives and decompile scripts; less disk use |
| `-ToolCache PATH` | Reuse a cached Smithbox release ZIP only after checksum verification |
| `-NonInteractive` | Requires both folder arguments; no folder pickers |

The game folder must contain `CUSA03173/uroot/sce_sys/param.sfo`, `eboot.bin`,
and `dvdroot_ps4`. SFO title/version and representative required assets are checked.
Sibling update/DLC folders are not merged. Setup does not decrypt protected content,
download game files, or install the separately supplied US DLC package.

Rerun the same command after an interrupted download/build. Extraction keeps a SQLite
index and preserves existing files whose bytes differ. Decompilation writes to a separate
tree and preserves edited references; edit your own mod project instead. A crash may
leave `.unpack.lock` or `.decompile.lock`; verify no extraction process is running before
removing only that lock. Reports identify unsupported formats and failed scripts.
Failures produce a nonzero exit code; a finished pass does not establish decompiler
semantic equivalence.

Raw extraction caching checks file size/mtime, while new writes and catalog inputs are
hashed. It is a resume optimisation, not a forensic integrity proof. To regenerate after
changing the selected game or extraction mode, use a separate checkout or preserve and
rename the previous `game-unpacked/raw` directory before rerunning.

Local catalogs and compatibility profiles can be regenerated independently:

```powershell
python src/scripts/generate_items.py --game 'D:\Bloodborne\game\CUSA03173\uroot\dvdroot_ps4'
python src/scripts/generate_catalogs.py --game 'D:\Bloodborne\game\CUSA03173\uroot\dvdroot_ps4'
python src/scripts/generate_profiles.py 'D:\Bloodborne\game\CUSA03173\uroot\eboot.bin'
cargo test -p bbmod --features local-catalogs --locked
```

Character definitions use bounded 1.09 binary-layout checks. Community translated row
labels and named team dictionaries are not bundled; their SDK lookups remain unavailable.
Action-state name matches are lexical research leads, not proof of runtime semantics.

Smithbox reads the original `dvdroot_ps4` archives. Create a separate project under
`mods/`; do not use the raw export as its game root or save over original game files.
