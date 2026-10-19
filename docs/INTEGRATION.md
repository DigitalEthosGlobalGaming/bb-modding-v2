# Host integration

Use RUNTIME.md for the maintained build/deploy flow. The patch in
src/integration/bbport-modhost.patch targets bbport commit
652f9a494c47cc8c1ab16f28b456372eaa8103f8. It is a legacy bridge: newer SDK declarations
do not automatically gain runtime producers.

prepare_runtime.py materialises host and SDK into third_party/bbmod in a pinned, ignored
port checkout. It checks and applies the patch and records its hash. It does not modify
the installed runtime or the original development tree.

The game-thread sync point services host callbacks and lifecycle. The overlay invokes
panels through the host; file-open translation consults owned overrides. Changes require
unload quiescence and thread-safe ownership. See ARCHITECTURE.md and source contracts.

Native-menu/character compatibility profiles are generated locally from a user's
unencrypted SELF/ELF after verifying address-specific hashes. Tracked fallback profiles
fail compatibility checks. This does not wire backends missing from the pinned runtime.
No decryption keys or protection bypasses are provided.

See VALIDATION.md for checks performed on this toolkit. Historical notes are not proof
of gameplay behavior in a newly built runtime.
