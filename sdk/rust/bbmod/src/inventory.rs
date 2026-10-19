//! Read-only carried inventory, ABI 1.4. The host samples at most every 250 ms
//! when queried or subscribed. Pages copy a shared bounded cache; a revision
//! mismatch returns STATE rather than combining different inventory snapshots.
//! Changes may coalesce; a reduced quantity is not proof of item use.
use crate::{check,sys,Cx,Error,Event,Result};
pub use sys::bbm_inventory_entry as InventoryEntry;
pub use sys::bbm_inventory_summary as InventorySummary;
pub use sys::bbm_inventory_change as InventoryChange;
pub const AVAILABLE:u32=sys::BBM_INVENTORY_AVAILABLE;
pub const ID_RESOLVED:u32=sys::BBM_INVENTORY_ID_RESOLVED;
pub const PAGE_SIZE:u32=sys::BBM_INVENTORY_PAGE_SIZE as u32;
pub const MAX_ITEMS:u32=sys::BBM_INVENTORY_MAX_ITEMS as u32;
pub mod event {pub const INVENTORY_CHANGED:u32=crate::sys::BBM_EV_INVENTORY_CHANGED;}
#[derive(Debug,Clone)]
pub struct InventoryPage {pub snapshot:InventorySummary,pub offset:u32,pub entries:Vec<InventoryEntry>}
#[derive(Debug,Clone)]
pub struct InventorySnapshot {pub snapshot:InventorySummary,pub entries:Vec<InventoryEntry>}
/// Equipment references joined to this cached inventory by exact handle.
/// Missing/ambiguous/unresolved references stay None; no hand names are inferred.
#[derive(Debug,Clone)]
pub struct EquippedInventory {
    pub equipment:crate::equipment::EquipmentSnapshot,
    pub inventory:InventorySummary,
    pub slots:[Option<InventoryEntry>;16],
}
impl EquippedInventory {
    pub fn selected_even(&self)->Option<&InventoryEntry>{self.slots.get(self.equipment.selected_even as usize)?.as_ref()}
    pub fn selected_odd(&self)->Option<&InventoryEntry>{self.slots.get(self.equipment.selected_odd as usize)?.as_ref()}
}
fn join_equipment(equipment:crate::equipment::EquipmentSnapshot,inventory:&InventorySnapshot)->Result<EquippedInventory>{
    if equipment.flags&crate::equipment::AVAILABLE==0||inventory.snapshot.flags&AVAILABLE==0||
        equipment.generation!=inventory.snapshot.generation||equipment.actor!=inventory.snapshot.actor||equipment.character!=inventory.snapshot.character{return Err(Error::STATE);}
    let mut slots=[None;16];
    for (i,reference) in equipment.runtime_references.iter().enumerate(){
        if *reference==0{continue;}
        let mut matches=inventory.entries.iter().filter(|e|e.reference==*reference);
        if let Some(entry)=matches.next(){if matches.next().is_none()&&entry.flags&ID_RESOLVED!=0{slots[i]=Some(*entry);}}
    }
    Ok(EquippedInventory{equipment,inventory:inventory.snapshot,slots})
}
fn summary_valid(s:&InventorySummary)->bool {
    s.size as usize==core::mem::size_of::<InventorySummary>()&&s.item_count<=MAX_ITEMS&&s.slot_count<=MAX_ITEMS&&s.item_count<=s.slot_count
}
fn unpack_page(raw:sys::bbm_inventory_page,requested_revision:u64,offset:u32,limit:u32)->Result<InventoryPage>{
    if !summary_valid(&raw.snapshot)||raw.snapshot.flags&AVAILABLE==0||raw.snapshot.revision==0||raw.offset!=offset||raw.count>limit||raw.count>PAGE_SIZE||
        raw.offset>raw.snapshot.item_count||raw.count>raw.snapshot.item_count-raw.offset||
        (requested_revision!=0&&raw.snapshot.revision!=requested_revision){return Err(Error::STATE);}
    Ok(InventoryPage{snapshot:raw.snapshot,offset:raw.offset,entries:raw.entries[..raw.count as usize].to_vec()})
}
impl Cx {
    /// Read at most 64 occupied entries, skipping empty slots. Pass revision=0 for
    /// the first page, then its returned revision to reject a changed cache.
    /// Entries retain opaque runtime references and metadata; use encoded_item_id
    /// only when flags & ID_RESOLVED != 0. This does not enumerate storage.
    pub fn inventory_page(&self,revision:u64,offset:u32,limit:u32)->Result<InventoryPage>{
        if limit==0||limit>PAGE_SIZE||offset>MAX_ITEMS{return Err(Error::ARG);}
        let h=unsafe{&*self.host};if !h.has_inventory(){return Err(Error::UNSUPPORTED);}
        let mut raw=sys::bbm_inventory_page::default();
        check(unsafe{(h.inventory_page)(self.me,revision,offset,limit,&mut raw)})?;
        unpack_page(raw,revision,offset,limit)
    }
    /// Correlate equipment with inventory by exact reference, checking matching
    /// player generation/actor/record and unchanged equipment before/after reads.
    /// Cached inventory can lag 250 ms: a missing match is None, never a guessed ID.
    /// selected_even/odd remain neutral labels until hand mapping is verified.
    pub fn equipped_inventory(&self)->Result<EquippedInventory>{
        let before=self.equipment_snapshot()?;
        let inventory=self.inventory_snapshot()?;
        let after=self.equipment_snapshot()?;
        if before!=after{return Err(Error::STATE);}
        join_equipment(after,&inventory)
    }
    /// Obtain an owned full copy, bounded to 4096 entries. A refresh during
    /// pagination fails with STATE; callers may retry later, never loop forever.
    pub fn inventory_snapshot(&self)->Result<InventorySnapshot>{
        let first=self.inventory_page(0,0,PAGE_SIZE)?;
        let snapshot=first.snapshot;let mut entries=first.entries;
        entries.reserve(snapshot.item_count as usize-entries.len());
        while entries.len()<(snapshot.item_count as usize){
            let page=self.inventory_page(snapshot.revision,entries.len() as u32,PAGE_SIZE)?;
            if page.snapshot!=snapshot||page.entries.is_empty(){return Err(Error::STATE);}
            entries.extend(page.entries);
        }
        Ok(InventorySnapshot{snapshot,entries})
    }
}
impl Event<'_>{
    pub fn inventory_change(&self)->Option<InventoryChange>{
        if self.kind!=event::INVENTORY_CHANGED||self.data.len()!=core::mem::size_of::<InventoryChange>(){return None;}
        let v=unsafe{core::ptr::read_unaligned(self.data.as_ptr().cast::<InventoryChange>())};
        if !summary_valid(&v.current)||(v.previous.size!=0&&!summary_valid(&v.previous)){return None;}
        Some(v)
    }
}
#[cfg(test)]mod tests{
    use super::*;
    #[test]fn layout_and_payload(){
        assert_eq!(core::mem::size_of::<InventoryEntry>(),32);
        assert_eq!(core::mem::size_of::<InventorySummary>(),48);
        assert_eq!(core::mem::size_of::<InventoryChange>(),96);
        assert_eq!(core::mem::size_of::<sys::bbm_inventory_page>(),2104);
        assert!(Event{kind:event::INVENTORY_CHANGED,a:0,b:0,data:&[]}.inventory_change().is_none());
    }
    fn page()->sys::bbm_inventory_page{
        let mut p=sys::bbm_inventory_page::default();p.snapshot=InventorySummary{size:48,flags:AVAILABLE,revision:1,slot_count:100,item_count:70,..Default::default()};p.count=64;p
    }
    #[test]fn exact_equipment_join_and_identity_guard(){
        let mut equipment=crate::equipment::EquipmentSnapshot{flags:1,generation:4,actor:100,character:200,selected_even:0,selected_odd:1,..Default::default()};
        equipment.runtime_references[0]=0x80800002;equipment.runtime_references[1]=123;
        let mut inventory=InventorySnapshot{snapshot:InventorySummary{flags:1,generation:4,actor:100,character:200,..Default::default()},entries:vec![InventoryEntry{reference:0x80800002,encoded_item_id:1001000,flags:ID_RESOLVED,..Default::default()}]};
        let joined=join_equipment(equipment,&inventory).unwrap();assert_eq!(joined.selected_even().unwrap().encoded_item_id,1001000);assert!(joined.selected_odd().is_none());
        inventory.entries.push(inventory.entries[0]);assert!(join_equipment(equipment,&inventory).unwrap().selected_even().is_none());
        inventory.entries.pop();inventory.entries[0].flags=0;assert!(join_equipment(equipment,&inventory).unwrap().selected_even().is_none());
        inventory.snapshot.generation+=1;assert!(join_equipment(equipment,&inventory).is_err());
    }
    #[test]fn checked_pagination(){
        assert_eq!(unpack_page(page(),0,0,64).unwrap().entries.len(),64);
        assert!(unpack_page(page(),2,0,64).is_err());
        let mut p=page();p.count=65;assert!(unpack_page(p,1,0,64).is_err());
        let mut p=page();p.offset=64;p.count=7;assert!(unpack_page(p,1,64,64).is_err());
        let mut p=page();p.offset=64;p.count=6;assert_eq!(unpack_page(p,1,64,64).unwrap().entries.len(),6);
        let mut p=page();p.snapshot.item_count=5000;assert!(unpack_page(p,1,0,64).is_err());
    }
}
