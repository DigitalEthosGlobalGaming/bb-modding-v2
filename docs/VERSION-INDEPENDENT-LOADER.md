# Running on any bbport version

Status: analysis and proposal, 2026-10-23. Nothing in the "Sidecar" section is built.

## The problem

The mod loader (`src/host`) is linked into `bb-probe.exe` by a patch applied to bbport's source. A build therefore
only fits the release it was made from, and every bbport release needs its own runtime. Version 1.9.4 already broke
the 1.9.2 build (its patch file format changed).

## What bbport's own modding support offers

Checked in upstream `docs/MODS.md` (tag `windows-v1.9.4`):

- **File mods:** folders under `mods/` with a `dvdroot_ps4/` tree replace or add game files (textures, models,
  params, UI, sound, fonts), with a load order in `mods.json`.
- **Code patches:** XML patches in shadPS4/GoldHEN format that write bytes into the game's executable.
- **Not supported, in its words:** DLL injectors, script plugins, replacing `eboot.bin` or `sce_module`.

So it cannot load our native mods. It can carry the data half of a mod (file overlays). `bbmod.compiled_content` is
exactly that, and our file-override feature could be implemented by writing into upstream's `mods/` folder instead
of patching bbport.

## Option 1: keep patching, automate it (built)

One runtime per release, chosen by the hash of the installed `bb-probe.exe`. New in this change:

- Adding a release is a data change in `tools.lock.json`. The installer workflow reads that list and builds every
  runtime in parallel, so there are no per-version workflow edits.
- `python src/scripts/add_runtime.py --latest` finds the newest unsupported release, checks that the patch applies and
  adds it. A daily workflow (`bbport-watch.yml`) runs it, opens a pull request when the patch still applies and an
  issue when a human has to rebase it. The original patch applied to 1.9.2; moving to 1.9.4 took a rebase of two small
  hunks, and that patch also applies to 1.9.3. So some releases will be a pull request and some an issue.
- An unsupported version is refused with nothing changed, and an updated bbport is never overlaid with an older build.

Limits: a release is supported only after someone merges the pull request and ships an installer, and a compiled
runtime has not been played. This stays "per release", never "any release".

## Option 2: a sidecar loader (the only real "any version")

Load the host from outside `bb-probe.exe`, using only things that do not change between releases. The host is already
an embeddable library: `modhost_config` takes the pieces it needs from the application as callbacks. The sidecar would
supply them.

| Host needs | How a sidecar could supply it | Notes |
| --- | --- | --- |
| Get loaded into the process | A proxy `dxgi.dll` (or `d3d12.dll`, `bcrypt.dll`) in `bin/` | Verified: `bb-probe.exe` imports `dxgi.dll`, `d3d12.dll`, `bcrypt.dll`, `CRYPT32.dll`, `IPHLPAPI.dll`, `vulkan-1.dll`, `libZydis.dll`; none are in Windows' KnownDLLs, so the app folder wins. |
| Game memory, hooks, patches | In-process; the PS4 code runs natively | The eboot is found by scanning for the image; no bbport symbols needed |
| Keyboard, mouse, pad state, input capture | Hook the SDL3 functions bbport imports from `SDL3.dll` | SDL's exported API is stable across versions |
| Pad effects and lightbar | SDL's gamepad effect calls | |
| Text entry | SDL text input | |
| Draw the menu | Hook `vkQueuePresentKHR` (Vulkan layer or proxy) and draw with our own renderer | The biggest piece: fonts, rectangles, images |
| File overrides | Write into upstream's `mods/` folder | Static at startup, not runtime |
| USB grip haptics | Talk to the HID device directly | |

Risks: it is a new component (several weeks of work to reach today's feature set), anti-tamper or timing in the game
process, hooking `SDL3.dll` and Vulkan presentation across driver and bbport changes, and features that today rely on
bbport internals (native menu, the shared text lease with the game's own IME) would need new approaches. Each feature
would be gated on a capability check like today's.

## Recommendation

Use Option 1 now, which is built. Start Option 2 only if the number of bbport releases to track becomes a burden, and
start it as a spike with a single goal: a proxy `dxgi.dll` that loads the existing host, reads the keyboard through an
SDL hook and draws the Mod Manager through a Vulkan present hook, on stock 1.9.4 with no patched `bb-probe.exe`.
If that works, the remaining capabilities are incremental.
