> Historical workspace notes. Paths and validation describe the original workspace; use ../SETUP.md for this toolkit. Inspector and bulk recovered tables are excluded.

# Human-readable item catalog

The portable mapping is [items.json](../../sdk/rust/bbmod/local-data/items.json).
It maps snake_case keys to names, categories, parameter IDs and category-encoded
item IDs. It was generated read-only from this installation's CUSA03173 1.09
English name archives and equipment parameter tables; source hashes are included.

Examples:

| Key | Display name | Parameter ID | Encoded item ID |
| --- | --- | --- | --- |
| `saw_cleaver` | Saw Cleaver | 7000000 | 7000000 |
| `saw_cleaver_plus_1` | Saw Cleaver +1 | 7000100 | 7000100 |
| `blood_vial` | Blood Vial | 1000 | 1073742824 (`0x400003E8`) |

Use `encoded_item_id` when comparing to a resolved inventory entry. `reference`
is a runtime handle and must not be used as a catalog ID. Parameter IDs alone are
only unique within their category. The category masks are weapon `0`, protector
`0x10000000`, accessory `0x20000000` and goods `0x40000000`.

## Rust SDK

```rust
use bbmod::{inventory, items};

let cleaver = items::by_key("saw_cleaver").unwrap();
assert_eq!(cleaver.encoded_item_id, items::ids::SAW_CLEAVER);
assert_eq!(items::by_id(7_000_000).unwrap().name, Some("Saw Cleaver"));

// Inside a mod callback with a Cx:
// for entry in cx.inventory_snapshot()?.entries {
//     if entry.flags & inventory::ID_RESOLVED != 0 {
//         if let Some(item) = items::by_id(entry.encoded_item_id) {
//             bbmod::info!(cx, "{}: {}", item.key, entry.quantity);
//         }
//     }
// }
```

`items::ITEMS` exposes all entries. `items::JSON` provides the JSON as an embedded
string. Both lookup functions use compiled binary-search indexes with no heap
allocation, disk access or game-memory scan. No host ABI or runtime update is
required; rebuild a mod against this SDK to use the catalog.

## Coverage and key rules

There are 1,699 parameter-backed rows: 1,385 named, 144 missing names and 170
placeholder names. Name-only FMG entries without a corresponding parameter row
are excluded. Existing rows do not prove obtainability or that granting them is
safe. This catalog provides identification, not item-spawning functionality.

- Keys use English names in snake_case, removing apostrophes. Explicit upgrades
  use `plus_1`, etc.; Lost and Uncanny variants retain their names.
- Duplicate names receive a category and parameter-ID suffix for every matching
  row, e.g. `hunter_garb_protector_<id>`. No ambiguous short alias is selected.
- Missing/placeholder names use `unnamed_<category>_<id>`. `name_status` is
  `named`, `missing` or `placeholder`; `name` retains the source label or null.
- Accessories without exact name matches remain unnamed; no rune/gem names are
  inferred. Unknown IDs return `None`; no upgrade arithmetic or fuzzy matching
  is performed.
- Keys are deterministic for these source archives. Review regenerated key
  changes as an API compatibility change before replacing the catalog.

## Regeneration and checks

From the toolkit root, with `$GameAssets` set to the selected `dvdroot_ps4`:

```powershell
python src/scripts/generate_items.py --game $GameAssets
python src/scripts/generate_items.py --game $GameAssets --check
cargo test -p bbmod --features local-catalogs
```

The generator writes JSON and its Rust mirror together. Do not edit either
generated file independently. It reads the extracted archives without modifying
them or starting the game. Other builds/languages need a separately reviewed
catalog; changing the input path alone does not verify their compatibility.
