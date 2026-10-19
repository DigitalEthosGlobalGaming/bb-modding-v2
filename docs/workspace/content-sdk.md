> Historical workspace notes. Paths and validation describe the original workspace; use ../SETUP.md for this toolkit. Inspector and bulk recovered tables are excluded.

# Content registry and typed hooks

The offline content compiler owns item-row allocation and asset overlays. SDK declarations do not create rows inside a running game. Compile and activate the resulting content before using its registry or granting its items.

Include the compiler's generated Rust registry in your mod:

```rust,ignore
mod compiled { include!("generated/items.rs"); }
let registry = bbmod::content::Registry::new(compiled::ITEMS)?;
let item = registry.get("example.ember_cleaver").ok_or(bbmod::Error::NOT_FOUND)?;
for fire_item in registry.by_tag("fire") { /* namespaced metadata */ }
let carried = cx.content_inventory(&registry, item.id)?;
```

IDs use dots. `bloodborne.saw_cleaver` belongs to the reserved vanilla namespace; `example.ember_cleaver` is distinct. Upgrades have exact explicit IDs such as `example.ember_cleaver_plus_1`. Registry lookups never normalize upgrades or resolve inventory runtime references as parameter IDs. Inventory matching requires the host's `ID_RESOLVED` flag and an exact category-encoded ID. The carried inventory is sampled and excludes storage. A registry entry alone does not prove the content is installed.

`ItemType` supports Weapon, Armor, Accessory, Goods, Consumable, KeyItem and Material. The latter three are Goods at the engine category level. Vanilla Goods retain that coarse type unless their meaning is established. Namespaces and item names start with a lowercase ASCII letter and contain lowercase letters, digits or underscores. IDs have exactly two components. Tags start with a lowercase letter and also allow dots and hyphens. Registry construction checks duplicate names/encoded IDs, category prefixes and type/category consistency.

A build tool can create a manifest without a JSON dependency:

```rust,ignore
use bbmod::content::{ContentManifest, NewItem, EditItem, ItemType, FieldEdit};
let mut manifest = ContentManifest::new("example")?;
manifest.add_item(NewItem::new(
    "example.ember_cleaver", "bloodborne.saw_cleaver", ItemType::Weapon,
    "Ember Cleaver",
)?.description("A distinct cleaver using its base assets.")
 .tag("fire")?.upgrades(true).field("attackBaseFire", 40)? )?;
manifest.edit_item(EditItem::new("bloodborne.saw_cleaver")?
    .upgrades(true).field("attackBasePhysics", FieldEdit::Multiply(1.1))?)?;
std::fs::write("content.json", manifest.to_json()?)?;
```

Field existence, numeric ranges, cross-mod collisions, upgrade expansion and asset validity are compiler checks. The builder rejects malformed names, duplicate fields/items and non-finite numbers; it is not a replacement for those compiler checks. Custom asset declarations use the compiler's manifest asset section; the SDK builder currently emits only items and edits.

For attack callbacks, store a `CombatHooks` in the mod and register once during load:

```rust,ignore
let mut hooks = bbmod::content::CombatHooks::default();
hooks.on_attack(cx, |cx, attack| {
    cx.log_info(&format!("behavior {} source {}", attack.behavior_id, attack.source));
})?;
// In Mod::event:
self.hooks.dispatch(cx, event);
```

These callbacks observe queued native behavior events from two verified call sites. They are not hit confirmation, weapon identification, an interception point, or a guarantee of one callback per attack. Both sources can describe related actions. Callback panics are isolated; host subscriptions remain mod-owned and disappear on unload. `supports_hit()` returns false because no verified hit producer exists. Do not implement damage-on-hit by interpreting an HP decrease or every attack observation as a confirmed hit.

For an explicit item award, use `cx.give_content_item(&registry, "example.ember_cleaver", 1, player.generation)?`. This queues work on the verified native update point and does not install missing rows/assets. Poll `cx.item_give_status(request)` until terminal; terminal polling consumes the result. Submitted means the native award routine ran, not guaranteed inventory insertion or save commitment. Observe carried inventory afterwards. Goods quantities are 1..99; equipment quantity is 1. The host limits outstanding requests and expires pending work after five seconds. Pending work is cancelled on mod unload; a running award cannot be cancelled. Awards can persist in normal game saves.
