> Historical workspace notes. Paths and validation describe the original workspace; use ../SETUP.md for this toolkit. Inspector and bulk recovered tables are excluded.

# Character definitions and live references

The SDK includes an offline catalog of all 6,064 baseline `NpcParam` rows from
CUSA03173 1.09. Of these, 1,655 rows have English display names in NPC-name FMG
18. Shared variants remain separate definitions. Lookups are allocation-free and
do not scan game memory or read JSON at startup.

```rust
let definition = bbmod::get_character_definition(500000).unwrap();
assert_eq!(definition.name, Some("Cleric Beast"));
assert_eq!(definition.key, "bloodborne.npc_500000");
assert!(definition.description.is_none());
```

`get_character_definition(id)` accepts an exact **NPC parameter row ID**.
`character_definitions::by_id(NpcParamId(id))` is the typed equivalent;
`by_key("bloodborne.npc_500000")` uses a stable namespaced key. Unknown IDs return
None. `by_name_id(id)` returns every matching definition, since a name ID is
shared by variants and cannot identify one NPC row uniquely.

Definitions contain `id`, `key`, `name_id`, `name`, `description`, `internal_name`,
`name_status`, `base_health` and `team_type`. The description is None because these
sources contain no general character-description table. Missing display names
stay None. Internal development row names are available separately, often in
Japanese; they are not presented as English display names. Base health/team are
baseline parameter values and can differ from live scaling, effects or overlays.

The numeric ID namespaces are distinct:

| Identifier | Meaning | Lookup |
| --- | --- | --- |
| NPC parameter ID | A definition shared by instances | `get_character_definition(id)` |
| Name ID | Text shared by definitions | `character_definitions::by_name_id(id)` |
| Runtime handle | A loaded character-set entry | `snapshot.by_handle(CharacterHandle(id))` |
| Snapshot reference | An observed epoch/token/handle | `snapshot.get(reference)` or `cx.get_character(reference)` |
| Map entity ID | A map/script placement | General SDK resolver not implemented |

NPC row 1000 exists but has no English display name. Text name ID 1000 belongs to
a different lookup and must not be substituted for that row. A hexadecimal
handle shown by the observer is not an NPC definition ID.

```rust,ignore
use bbmod::characters::{CharacterExt, CharacterHandle, NpcParamId};
let snapshot = cx.characters()?;
for character in snapshot.by_definition(NpcParamId(500000)) {
    let definition = character.definition();
    let reference = snapshot.reference(CharacterHandle(character.handle)).unwrap();
    // Later: reject replaced/unloaded observed lifetimes against a fresh sample.
    let current = cx.get_character(reference)?;
}
```

`CharacterExt::definition()` requires the native `NPC_PARAM` validity flag and
looks up the exact NPC row; unresolved metadata returns None. Several live
characters may share a definition, so `by_definition` returns an iterator.
`CharacterRef` is read-only sampled identity. Changed epochs/tokens or missing
entries are rejected. The cache can lag by 250 ms; native memory reuse entirely
between samples remains undetectable. References are not native pointers, write
authorization, or persistent IDs across sessions.

The Enemy and Boss Observer uses definition names where present, falling back
to known boss labels or generic class labels. Sorting/search uses these names.
Live overlays that change NPC/name data are not automatically reflected in this
baseline catalog. The observer now uses separately marked community reference
labels when official names are missing; see [team/script and enemy-name references](CHARACTER-REFERENCES.md).

## Item names in hook tests

Hook Test A and B display names for resolved inventory entries and equipment
joined by exact inventory reference. Opaque unresolved references remain labelled
unresolved. Saved equipment IDs remain diagnostics rather than guessed weapon
identities. Attack behavior IDs are not looked up as item IDs.

The current native item-use producer carries a raw Goods parameter ID.
Disassembly shows RVA 0x18c91a7 calling Goods lookup 0x1f1e3d0, then forwarding
the preserved ID through stack -0xd8 to logging call 0x18c9353. The UI adds the
explicit Goods category for catalog lookup. Goods ID 1000 resolves to Blood Vial;
it is distinct from encoded ID 0x400003e8 and from an opaque inventory reference.
`items::by_param_id(Category::Goods, 1000)` and `items::encoded_id(category, id)`
expose this explicit conversion in the SDK.

The hook-test build embeds the current compiled content registry when available,
so custom names such as Ember Cleaver resolve too. `BB_CONTENT_REGISTRY` can
select another generated Rust registry; otherwise the build script uses the
current content workspace. Direct Cargo builds without that variable use the
vanilla catalog. Rebuild when compiled item metadata changes. Inventory/equipment
names refresh at most every 250 ms outside the draw callback.

## Data and validation

- JSON: `../../sdk/rust/bbmod/local-data/characters.json`
- Generator: `src/scripts/generate_catalogs.py --game GAME_ASSETS` from the toolkit root
- Definitions: exact NPC IDs joined to English NPC-name text by nameId.
- Internal names: bounded UTF-16 reads from parameter row-name strings.
- Generated output remains private under the SDK local-data directory.

Three Python tests cover source/generated agreement, missing text, row-name
bounds and escaping. SDK tests cover known/unknown rows, shared names, indexes
and stale references. Both real hook-test DLLs pass equipment/inventory/item-use
name checks alongside existing ownership/reload tests. The updated character
observer passes its two-DLL table/reload harness. No game was launched here.
