//! Namespaced metadata and offline content declarations. A registry does not install rows.
use std::collections::BTreeMap;
use crate::{Cx, Error, Result};
use crate::items::Category;
#[derive(Debug,Clone,Copy,PartialEq,Eq)]
pub enum ItemType { Weapon, Consumable, KeyItem, Material, Armor, Accessory, Goods }
impl ItemType {
    pub fn as_str(self)->&'static str {match self {Self::Weapon=>"weapon",Self::Consumable=>"consumable",Self::KeyItem=>"key_item",Self::Material=>"material",Self::Armor=>"armor",Self::Accessory=>"accessory",Self::Goods=>"goods"}}
    pub fn category(self)->Category {match self {Self::Weapon=>Category::Weapon,Self::Armor=>Category::Protector,Self::Accessory=>Category::Accessory,_=>Category::Goods}}
}
#[derive(Debug,Clone,Copy)]
pub struct ItemDefinition<'a> {pub id:&'a str,pub item_type:ItemType,pub category:Category,pub encoded_item_id:u32,pub name:Option<&'a str>,pub tags:&'a [&'a str],pub base:Option<&'a str>,pub upgrade_level:u8}
fn token(s:&str)->bool {s.as_bytes().first().is_some_and(u8::is_ascii_lowercase)&&s.bytes().all(|b|b.is_ascii_lowercase()||b.is_ascii_digit()||b==b'_')}
fn identifier(s:&str)->bool {s.split('.').count()==2&&s.split('.').all(token)}
fn tag(s:&str)->bool {s.as_bytes().first().is_some_and(u8::is_ascii_lowercase)&&s.bytes().all(|b|b.is_ascii_lowercase()||b.is_ascii_digit()||b"_.-".contains(&b))}
#[derive(Debug)]
pub struct Registry<'a> {items:&'a [ItemDefinition<'a>],ids:BTreeMap<&'a str,usize>,encoded:BTreeMap<u32,usize>}
impl<'a> Registry<'a> {
    pub fn new(items:&'a [ItemDefinition<'a>])->Result<Self>{
        let mut r=Self{items,ids:BTreeMap::new(),encoded:BTreeMap::new()};
        for (i,x) in items.iter().enumerate(){
            let prefix=match x.category {Category::Weapon=>0,Category::Protector=>0x10000000,Category::Accessory=>0x20000000,Category::Goods=>0x40000000};
            if !identifier(x.id)||x.item_type.category()!=x.category||x.encoded_item_id&0xf0000000!=prefix||x.base.is_some_and(|b|!identifier(b))||x.upgrade_level>10||x.tags.iter().any(|t|!tag(t)){return Err(Error::ARG);}
            if x.tags.iter().enumerate().any(|(i,t)|x.tags[..i].contains(t)){return Err(Error::CONFLICT);}
            if r.ids.insert(x.id,i).is_some()||r.encoded.insert(x.encoded_item_id,i).is_some(){return Err(Error::CONFLICT);}
        } Ok(r)
    }
    pub fn get(&self,id:&str)->Option<&'a ItemDefinition<'a>>{self.ids.get(id).map(|i|&self.items[*i])}
    pub fn by_encoded_id(&self,id:u32)->Option<&'a ItemDefinition<'a>>{self.encoded.get(&id).map(|i|&self.items[*i])}
    pub fn by_tag<'s>(&'s self,tag:&'s str)->impl Iterator<Item=&'a ItemDefinition<'a>>+'s{self.items.iter().filter(move|x|x.tags.contains(&tag))}
    pub fn by_type(&self,kind:ItemType)->impl Iterator<Item=&'a ItemDefinition<'a>>+'_ {self.items.iter().filter(move|x|x.item_type==kind)}
    pub fn inventory_matches(&self,id:&str,snapshot:&crate::inventory::InventorySnapshot)->Result<Vec<crate::inventory::InventoryEntry>>{
        let item=self.get(id).ok_or(Error::NOT_FOUND)?;
        Ok(snapshot.entries.iter().filter(|e|e.flags&crate::inventory::ID_RESOLVED!=0&&e.encoded_item_id==item.encoded_item_id).copied().collect())
    }
}
impl Cx {
    /// Queue an already compiled/installed item on the verified native update point.
    /// Poll item_give_status and verify inventory separately; this can persist in saves.
    pub fn give_content_item(&self,registry:&Registry<'_>,id:&str,quantity:u32,generation:u64)->Result<crate::item_actions::ItemGiveRequest>{
        let item=registry.get(id).ok_or(Error::NOT_FOUND)?;
        self.give_item_raw(item.encoded_item_id,quantity,generation)
    }
    pub fn content_inventory(&self,registry:&Registry<'_>,id:&str)->Result<Vec<crate::inventory::InventoryEntry>>{registry.get(id).ok_or(Error::NOT_FOUND)?;registry.inventory_matches(id,&self.inventory_snapshot()?)}}
/// An offline numeric field value. Integers remain exact in emitted JSON.
#[derive(Debug,Clone,Copy)]
pub enum Number { Integer(i64), Float(f64) }
impl Number {fn json(self)->Result<String>{match self {Self::Integer(v)=>Ok(v.to_string()),Self::Float(v) if v.is_finite()=>Ok(v.to_string()),_=>Err(Error::ARG)}}}
impl From<i32> for Number {fn from(v:i32)->Self{Self::Integer(v.into())}}
impl From<u32> for Number {fn from(v:u32)->Self{Self::Integer(v.into())}}
impl From<i64> for Number {fn from(v:i64)->Self{Self::Integer(v)}}
impl From<f64> for Number {fn from(v:f64)->Self{Self::Float(v)}}
#[derive(Debug,Clone)]
pub struct NewItem {id:String,base:String,kind:ItemType,name:String,description:String,tags:Vec<String>,fields:BTreeMap<String,Number>,upgrades:bool}
impl NewItem {
    pub fn new(id:&str,base:&str,kind:ItemType,name:&str)->Result<Self>{if !identifier(id)||!identifier(base)||name.is_empty(){return Err(Error::ARG);}Ok(Self{id:id.into(),base:base.into(),kind,name:name.into(),description:String::new(),tags:vec![],fields:BTreeMap::new(),upgrades:false})}
    pub fn description(mut self,value:&str)->Self{self.description=value.into();self}
    pub fn tag(mut self,value:&str)->Result<Self>{if !tag(value){return Err(Error::ARG);}if !self.tags.iter().any(|s|s==value){self.tags.push(value.into());}Ok(self)}
    pub fn upgrades(mut self,value:bool)->Self{self.upgrades=value;self}
    pub fn field(mut self,name:&str,value:impl Into<Number>)->Result<Self>{let value=value.into();validate_field(name,value)?;if self.fields.insert(name.into(),value).is_some(){return Err(Error::CONFLICT);}Ok(self)}
}
#[derive(Debug,Clone,Copy)]
pub enum FieldEdit {Set(Number),Multiply(f64)}
#[derive(Debug,Clone)]
pub struct EditItem {item:String,upgrades:bool,fields:BTreeMap<String,FieldEdit>}
impl EditItem {
    pub fn new(item:&str)->Result<Self>{if !identifier(item){return Err(Error::ARG);}Ok(Self{item:item.into(),upgrades:false,fields:BTreeMap::new()})}
    pub fn upgrades(mut self,value:bool)->Self{self.upgrades=value;self}
    pub fn field(mut self,name:&str,value:FieldEdit)->Result<Self>{validate_field(name,match value{FieldEdit::Set(v)=>v,FieldEdit::Multiply(v)=>Number::Float(v)})?;if self.fields.insert(name.into(),value).is_some(){return Err(Error::CONFLICT);}Ok(self)}
}
fn validate_field(name:&str,value:Number)->Result<()>{if name.is_empty()||!name.bytes().all(|b|b.is_ascii_alphanumeric()||b==b'_'){return Err(Error::ARG);}value.json()?;Ok(())}
fn quote(s:&str)->String {let mut out=String::from("\"");for c in s.chars(){match c {'"'=>out.push_str("\\\""),'\\'=>out.push_str("\\\\"),c if c<' '=>out.push_str(&format!("\\u{:04x}",c as u32)),c=>out.push(c)}}out.push('"');out}
#[derive(Debug)]
pub struct ContentManifest {namespace:String,items:Vec<NewItem>,edits:Vec<EditItem>}
impl ContentManifest {
    pub fn new(namespace:&str)->Result<Self>{if namespace.split('.').next()==Some("bloodborne")||!token(namespace){return Err(Error::ARG);}Ok(Self{namespace:namespace.into(),items:vec![],edits:vec![]})}
    pub fn add_item(&mut self,item:NewItem)->Result<()>{if item.id.rsplit_once('.').map(|x|x.0)!=Some(self.namespace.as_str()){return Err(Error::ARG);}if self.items.iter().any(|x|x.id==item.id){return Err(Error::CONFLICT);}self.items.push(item);Ok(())}
    pub fn edit_item(&mut self,item:EditItem)->Result<()>{if self.edits.iter().any(|x|x.item==item.item){return Err(Error::CONFLICT);}self.edits.push(item);Ok(())}
    /// Compile this JSON with the offline content compiler before launching.
    pub fn to_json(&self)->Result<String>{
        let mut items=vec![];for x in &self.items {let mut fields=vec![];for(k,v)in &x.fields{fields.push(format!("{}:{}",quote(k),v.json()?));}items.push(format!("{{\"id\":{},\"base\":{},\"type\":{},\"name\":{},\"description\":{},\"tags\":[{}],\"fields\":{{{}}},\"upgrades\":{}}}",quote(&x.id),quote(&x.base),quote(x.kind.as_str()),quote(&x.name),quote(&x.description),x.tags.iter().map(|t|quote(t)).collect::<Vec<_>>().join(","),fields.join(","),x.upgrades));}
        let mut edits=vec![];for x in &self.edits {let mut fields=vec![];for(k,v)in &x.fields {let value=match *v {FieldEdit::Set(n)=>n.json()?,FieldEdit::Multiply(v)=>format!("{{\"multiply\":{}}}",Number::Float(v).json()?)};fields.push(format!("{}:{}",quote(k),value));}edits.push(format!("{{\"item\":{},\"upgrades\":{},\"fields\":{{{}}}}}",quote(&x.item),x.upgrades,fields.join(",")));}
        Ok(format!("{{\"schema_version\":1,\"namespace\":{},\"items\":[{}],\"edits\":[{}]}}",quote(&self.namespace),items.join(","),edits.join(",")))
    }
}
/// Queued observations only. No cancellation, hit confirmation or weapon identity.
#[derive(Default)]
pub struct CombatHooks {attacks:Vec<Box<dyn FnMut(&Cx,&crate::equipment::AttackObserved)>>}
impl CombatHooks {
    pub fn on_attack(&mut self,cx:&Cx,callback:impl FnMut(&Cx,&crate::equipment::AttackObserved)+'static)->Result<()>{cx.subscribe(crate::equipment::event::ATTACK_OBSERVED)?;self.attacks.push(Box::new(callback));Ok(())}
    /// A verified hit producer is not available in this SDK.
    pub fn supports_hit(&self)->bool{false}
    /// Call from Mod::event; each callback panic is isolated from later callbacks.
    pub fn dispatch(&mut self,cx:&Cx,event:&crate::Event<'_>){if let Some(attack)=event.attack_observed(){for callback in &mut self.attacks{crate::entry::guard("content attack callback",||callback(cx,&attack));}}}
}
#[cfg(test)]
mod tests {
 use super::*;
 const ITEMS:&[ItemDefinition<'static>]=&[ItemDefinition{id:"example.ember_cleaver",item_type:ItemType::Weapon,category:Category::Weapon,encoded_item_id:7100000,name:Some("Ember"),tags:&["fire"],base:Some("bloodborne.saw_cleaver"),upgrade_level:0}];
 #[test]fn registry_exact_names_and_types(){let r=Registry::new(ITEMS).unwrap();assert_eq!(r.get("example.ember_cleaver").unwrap().encoded_item_id,7100000);assert!(r.get("ember_cleaver").is_none());assert_eq!(r.by_tag("fire").count(),1);assert_eq!(r.by_type(ItemType::Goods).count(),0);assert!(Registry::new(&[ITEMS[0],ITEMS[0]]).is_err());let mut bad=ITEMS[0];bad.category=Category::Goods;assert!(Registry::new(&[bad]).is_err());}
 #[test]fn manifest_escapes_and_preserves_numbers(){let mut m=ContentManifest::new("example").unwrap();m.add_item(NewItem::new("example.ember_cleaver","bloodborne.saw_cleaver",ItemType::Weapon,"A\"\\\n").unwrap().field("attackBasePhysics",123).unwrap()).unwrap();m.edit_item(EditItem::new("bloodborne.saw_cleaver").unwrap().field("attackBasePhysics",FieldEdit::Multiply(1.1)).unwrap()).unwrap();let json=m.to_json().unwrap();assert!(json.contains("A\\\"\\\\\\u000a"));assert!(json.contains("\"attackBasePhysics\":123"));assert!(json.contains("{\"multiply\":1.1}"));assert!(ContentManifest::new("bloodborne").is_err());assert!(NewItem::new("example.x","bloodborne.x",ItemType::Weapon,"X").unwrap().field("x",f64::NAN).is_err());}
 #[test]fn compiler_compatible_identifiers_and_tags(){
 for id in ["x", "a.b.c", "1a.b", "a.1b", "a.B", ".x"] {let mut item=ITEMS[0];item.id=id;assert!(Registry::new(&[item]).is_err());}
 for namespace in ["a.b","1a","","bloodborne"] {assert!(ContentManifest::new(namespace).is_err());}
 let tags=["element.fire","two-handed"];let mut item=ITEMS[0];item.tags=&tags;assert!(Registry::new(&[item]).is_ok());
 let duplicates=["fire","fire"];item.tags=&duplicates;assert!(Registry::new(&[item]).is_err());
 item.tags=&[];item.upgrade_level=11;assert!(Registry::new(&[item]).is_err());
 assert!(NewItem::new("example.x","bloodborne.x",ItemType::Weapon,"X").unwrap().tag("element.fire").unwrap().tag("two-handed").is_ok());
 }
 #[test]fn unresolved_inventory_never_matches(){let r=Registry::new(ITEMS).unwrap();let mut entry=crate::inventory::InventoryEntry::default();entry.encoded_item_id=7100000;let mut snapshot=crate::inventory::InventorySnapshot{snapshot:Default::default(),entries:vec![entry]};assert!(r.inventory_matches("example.ember_cleaver",&snapshot).unwrap().is_empty());snapshot.entries[0].flags=crate::inventory::ID_RESOLVED;assert_eq!(r.inventory_matches("example.ember_cleaver",&snapshot).unwrap().len(),1);}
}
