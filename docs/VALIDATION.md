# Validation performed — 19 October 2026

Checks run on Windows in the newly assembled toolkit, not merely inherited reports:

| Check | Result |
| --- | --- |
| PowerShell setup with explicit folders, NonInteractive and ConfigureOnly | Pass |
| Game SFO/version and installed runtime directory validation | Pass: CUSA03173 01.09 |
| Pinned Smithbox release ZIP SHA-256 and extraction | Pass |
| DSLuaDecompiler/SoulsFormats source archive SHA-256 | Pass |
| Pinned NuGet package download/hash and local-feed restore | Pass |
| Fresh DSLuaDecompiler CLI build | Pass; upstream warnings retained in local build output |
| Full DCX/BND4 extraction | 26,913/26,913 sources done; 99,430 files, 51,736,345,223 bytes |
| Lua/HavokScript references | 1,025/1,025; zero failed/unsupported |
| Local item catalog | 1,699 rows |
| Local NPC/action dictionaries | 6,064 NPC definitions plus action dictionaries |
| Local executable profiles | All address-specific hashes verified before generation |
| Python toolkit/archive tests | 12/12 pass |
| Rust workspace tests without private catalogs | Pass: 25 SDK + 2 UI tests; 3 doc examples ignored |
| Rust SDK tests with locally generated catalogs | 25/25 pass; 3 doc examples ignored |
| Native CLANG64 host build with Zydis | Pass |
| Native CTest suite | 22/22 pass, about 429 seconds |
| Generated standalone example mod | Release DLL builds |
| Migrated adaptive-trigger, character-observer and two hook mods | Cargo check passes |
| Content extraction using local Smithbox XML schemas | Pass |
| Content balance example | 11 field edits across upgrades; rebuilt overlay and hashes verified |
| Pinned runtime source checkout and integration patch application | Pass |
| PowerShell syntax parsing for public scripts | Pass |
| Source-control publication audit | Pass; private roots and generated catalogs excluded |

The extraction report records no modified source game files. All outputs are inside
ignored toolkit folders; original game files, existing mod sources, saves and settings
were retained. Personal mod copies were migrated locally, not added to toolkit Git.

Native subprocess checks needed execution outside the agent sandbox because Ninja/Git
child processes stalled inside it. This is an execution-environment limitation, not a
reason to disable checks or bypass tool-download verification. .NET restore originally
encountered a TLS error; the implemented bootstrap now downloads the exact package over
verified HTTPS and builds from a checksum-verified local feed.

Not verified here: GUI folder-picker interaction, launching Smithbox, installing system
prerequisites on a clean Windows machine, a fresh full port rebuild, installed runtime
deployment/rollback against the real game, title screen, gameplay, decompiler semantic
equivalence, or newer runtime backends. Rollback safeguards were tested with temporary
fixtures. No real game deployment or launch was performed.

The selected installed executable did not expose the checked framework marker. That
marker is only indicative; capabilities remain unverified. The optional pinned runtime
build uses a legacy bridge, not all services represented by the latest SDK.

Raw logs, extraction indices, decompilation report and machine paths are in ignored
.local/ and game-unpacked/. No remote repository was created, no code was pushed,
and no source commit was created. The fresh main repository is ready for review.
