> Historical workspace notes. Paths and validation describe the original workspace; use ../SETUP.md for this toolkit. Inspector and bulk recovered tables are excluded.

> Imported workspace notes (18 October 2026). Paths to the adjacent port, mods,
> tools and runtime describe the original development workspace and may not exist
> in a standalone clone. The framework SDK is at ../../sdk/rust/bbmod from this folder.

# Developing Bloodborne mods

This is the developer entry point for this workspace's bbmod host and Rust SDK.
The SDK lives at `../bloodborne_pc/third_party/bbmod/sdk/rust/bbmod`; it is a local
path dependency, not a separately published package. Game-specific services are
versioned capabilities: a declaration or researched address is not proof that a
producer is implemented or that a native call is safe.

Start with [the development workflow](DEVELOPMENT.md), [shared hooks](SHARED-HOOKS.md),
and [validation](TESTING.md). Copy [AGENTS.example.md](AGENTS.example.md) into a new
mod's `AGENTS.md` when using a coding agent. The two runnable subscribers live in
[`../mods/hooks-testing-mod`](../mods/hooks-testing-mod/README.md).

Read the implementation references before changing framework code:

- [Integration quickstart](../bloodborne_pc/BBMOD-QUICKSTART.md)
- [Mod workflow](../bloodborne_pc/third_party/bbmod/docs/MODDING.md)
- [Threading and runtime integration](../bloodborne_pc/third_party/bbmod/docs/INTEGRATION.md)
- [Rust SDK](../bloodborne_pc/third_party/bbmod/docs/RUST_SDK.md)
- [BB Tools review and evidence](../BB-TOOLS-REVIEW.txt)
- [Equipment and native UI investigation](equipment-ui-research.md)
- [Native in-game Mods entry beside Inventory/Status/System](NATIVE-INGAME-MENU.txt)
- [Carried inventory research](inventory-research.md)
- [Human-readable item catalog and SDK lookups](ITEM-CATALOG.md)
- [Content extraction, item compilation and persistent IDs](CONTENT-PIPELINE.txt)
- [Namespaced item SDK and gameplay callbacks](content-sdk.md)
- [Native item awards and remaining hit-event research](item-gameplay-events.txt)
- [Enemy/boss observations and AI research](CHARACTERS-AND-AI.md)
- [Character definitions, live references and item-name displays](CHARACTER-DEFINITIONS.md)
- [Team/script references and discovering enemy names](CHARACTER-REFERENCES.md)
- [Map IDs, area names and script filename conventions](MAP-AND-SCRIPT-NAMES.md)
- [Shared UI example mod](../bloodborne_pc/third_party/bbmod/examples/ui-examples/README.md)
- [Return-to-game checklist](../TEST-WHEN-BACK.txt)
- [New item content test checklist](../TEST-ITEM-CONTENT.txt)

Build and test locally before deployment. Keep game assets, packages, saves and
user settings intact. Native DLLs run inside the game process; type-safe event
decoding does not make arbitrary memory writes or guessed function signatures safe.
