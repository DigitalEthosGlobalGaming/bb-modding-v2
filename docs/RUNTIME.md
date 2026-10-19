# Runtime integration and deployment

Selecting an installation validates its files; it does not prove ABI capabilities.
`doctor` reports an indicative framework marker and explicitly leaves capability/gameplay
verification false. A compatible running host must still pass SDK table-size/capability checks.

The reproducible source path pins Supermedo/bloodborne_pc
`652f9a494c47cc8c1ab16f28b456372eaa8103f8`, then applies the included legacy integration
patch. It supports overlay, shared input, hot reload and file overrides. It does not wire
all newer character/inventory/item-award/haptics/native-menu backends. SDK declarations
are not evidence those features work in that runtime. The newer local development
tree was not imported wholesale because it contains unresolved merges.

Install MSYS2 and build dependencies explicitly if needed:

```powershell
.\src\scripts\install-prerequisites.ps1 -RuntimeBuild
.\src\scripts\build-runtime.ps1 -InstallDependencies
```

With dependencies already available:

```powershell
.\bb.ps1 build-runtime
```

This prepares source under ignored `tools/bloodborne_pc`, initializes its pinned
submodules, builds separately and stages `bb-probe.exe` and required CLANG64 DLLs
under `.local/runtime-stage/bin`. Source edits in the prepared checkout should be
preserved, not discarded. Git/MSYS2 network or dependency failures stop the build.
The native package manager supplies current packages; those toolchain packages are
not locked to an historical MSYS2 snapshot.

Close the launcher and game. Review the stage before explicitly deploying:

```powershell
.\bb.ps1 deploy-runtime
.\bb.ps1 deploy-mod mods/me.my-mod
```

Deployment backs up every replaced file and records old/new hashes under
`.local/backups/<timestamp>`. It prints the exact rollback command:

```powershell
.\bb.ps1 rollback '.local/backups/<timestamp>'
```

Rollback refuses to overwrite files changed after deployment. New files created by that
deployment are removed; previous files are restored. Saves and settings are not part of
deployment or rollback. Existing mod manifests are preserved when redeploying a mod;
edit its installed configuration separately if necessary.

No automatic game launch, title-screen check or gameplay validation occurs during setup.
Before using a built runtime on an installation, validate it in isolation with your own
game data and confirm the launcher/runtime combination is compatible.
