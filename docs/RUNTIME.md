# Runtime integration and deployment

Selecting an installation validates its files; it does not prove ABI capabilities.
`doctor` reports an indicative framework marker and explicitly leaves capability/gameplay
verification false. A compatible running host must still pass SDK table-size/capability checks.

Several bbport releases are supported at once (see `runtimes` in `tools.lock.json` and `add_runtime.py`); each is
built with `.b.ps1 build-runtime -Runtime <id>` and staged in `.local/runtime-stage/<id>`. The text below describes one of them.

The reproducible source path pins Supermedo/bloodborne_pc
`b3fc78bff0997f75f73e14a5fffeb1141f0cea70` (the upstream keyboard/mouse merge),
then applies `src/integration/bbport-modhost.patch`. This brings the authored local
bridge forward without copying the old development repository or its private data.
The bridge wires overlay panels/tables, shared input, reload, file overrides, controller
trigger/lightbar effects, USB haptics, and shared game/mod text entry. The canonical
ABI 1.10 host is copied from `src/host` with the SDK from `sdk`.

Native character, inventory, item-award and native-menu services still require their
individual compatibility checks and private generated executable profiles where used.
Native UI also requires its asset overlay and explicit opt-in. Check capabilities at
runtime; compilation does not establish game semantics or successful item insertion.

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
under `.local/runtime-stage/bin`. Source edits in the prepared checkout are preserved. Preparation records its pin,
patch hash and hashes of copied framework files in `.bbmod-source.json`. Reuse checks
the complete bridge with a reverse patch check and refuses to overwrite edited copied
framework files. If the integration pin or patch changes, preserve the checkout and
prepare a fresh one; no reset/clean is performed. `prepare_runtime.py --target <path>`
can prepare another separate checkout. The build defaults to four jobs; invoke
`src/scripts/build-runtime.ps1 -Jobs <n>` to change this. Git/MSYS2 network or dependency failures stop the build.
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

## Validation of the current integration

On 2026-10-20 a fresh separate checkout of the exact pin accepted the patch,
prepared twice, and compiled all 395 Windows runtime targets using MSYS2 CLANG64
with Zydis and the canonical ABI 1.10 host. The stage manifest records exact executable
and dependency hashes; the toolchain packages are not a historical reproducibility lock.
The shared text lease and actual Orbis IME-source tests passed, including concurrent
lease acquisition, stale tokens, repeated cancellation/reload, UTF-16 results and
no-window fallback. The tests use a fake window backend and do not verify real SDL
window interaction. Build the explicit targets and run only those tests with:

```powershell
cmake --build tools/bloodborne_pc/out/gpu --target text-input-test ime-text-test --parallel 4
ctest --test-dir tools/bloodborne_pc/out/gpu -R 'runtime-(text-lease|ime-text)' --output-on-failure
```

The historical WIP merge index was resolved by keeping both launcher FOV and code-mod
setup, and both CRT fault diagnostics and mod fault attribution. Its pre-resolution
index, all conflict stages, binary diffs and dirty/untracked source copies remain in
private consolidation records. The WIP history and source archives must be retained;
they contain work outside the reviewed integration patch. No commit, deployment,
game launch, title-screen check or gameplay validation was performed for this build.
