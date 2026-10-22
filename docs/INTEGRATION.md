# Host integration

Use RUNTIME.md for the maintained build/deploy flow. The reviewed patch in
`src/integration/bbport-modhost.patch` targets upstream bbport commit
`b3fc78bff0997f75f73e14a5fffeb1141f0cea70`. It contains authored integration glue,
shared text-entry/IME code, haptics support, and two fake-window tests. It contains
no recovered game scripts, extracted assets, private catalogs or instruction profiles.
The upstream pin supplies the keyboard/mouse and GPU improvements already merged there.

`prepare_runtime.py` applies that patch in a separate checkout and materialises the
canonical host and SDK into `third_party/bbmod`. Its checkout-local stamp records the
pin, patch hash and hashes of copied files. Reuse checks the patch in reverse and
preserves edited copied sources. An integration change requires a fresh checkout;
`--target <path>` supports an alternate isolated checkout. The installed runtime and
preserved historical WIP repository are never preparation targets.

The game's pad-poll sync point services callbacks and lifecycle. The ImGui overlay
invokes panels through the host; file-open translation consults owned overrides.
Controller effects and bounded USB haptics have explicit backend callbacks. Mod and
game IME text requests use one locked lease, with token checks and deferred window-thread
cancellation acknowledgement so an old request cannot cancel a replacement dialog.
Changes require unload quiescence and thread-safe ownership; see ARCHITECTURE.md.

Native-menu/character compatibility profiles are generated locally from a user's
unencrypted SELF/ELF after verifying address-specific hashes. Tracked fallback profiles
fail compatibility checks. Native UI requires its asset overlay and explicit opt-in;
newer host functions remain capability/profile gated. No decryption keys or protection
bypasses are provided. SDK declarations alone do not establish backend availability.

The current Windows runtime built successfully in isolation, and the text lease and
actual IME-source synthetic tests passed on 2026-10-20. The historical cross-compiled
ImGui smoke harness has current toolkit paths and text/haptics stubs, but has not been
rerun as part of this validation. No deployment or gameplay check occurred. RUNTIME.md
records the exact scope, commands and preserved WIP provenance.
