# Mod development reference

Start with [the maintained mod workflow](../MODDING.md), [Rust API](../RUST_SDK.md)
and [runtime build guide](../RUNTIME.md). Canonical source is `sdk/`, `src/host/`
and `src/scripts/`; retained user projects remain outside the toolkit under
`../modding/mods/` and `../modding/bb-modding-adaptive-triggers/`.

The detailed documents in this directory record service contracts and historical
research. Old command paths and validation counts are historical evidence; use
the canonical guides for executable workflows and current validation claims.
Local catalogs are optional, private, and rebuilt from selected game inputs.
The public generator does not ship curated dictionaries or Inspector/Openborne
schemas. This installation preserves hash-verified private reference inputs under
`.local/reference-inputs/catalogs`; fresh installations may lack those labels.

- [Development](DEVELOPMENT.md)
- [Testing](TESTING.md)
- [Shared service contracts](SHARED-HOOKS.md)
- [Character observations](CHARACTERS-AND-AI.md)
- [Character definitions](CHARACTER-DEFINITIONS.md)
- [Character reference research](CHARACTER-REFERENCES.md)
- [Map and script names](MAP-AND-SCRIPT-NAMES.md)
- [Content SDK](content-sdk.md)
