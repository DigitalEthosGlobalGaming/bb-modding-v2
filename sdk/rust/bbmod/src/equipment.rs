//! Sampled equipment diagnostics and native action observations (ABI 1.4).
//! Equipment references are opaque; saved IDs can lag live state. Named hand
//! mapping is unverified. Native behavior IDs are not weapon IDs.
use crate::{check, sys, Cx, Error, Event, Result};
pub use sys::bbm_equipment_snapshot as EquipmentSnapshot;
pub use sys::bbm_equipment_change as EquipmentChange;
pub use sys::bbm_item_use as ItemUse;
pub use sys::bbm_attack_observed as AttackObserved;
pub mod event {
    pub const EQUIPMENT_CHANGED: u32 = crate::sys::BBM_EV_EQUIPMENT_CHANGED;
    pub const ATTACK_OBSERVED: u32 = crate::sys::BBM_EV_ATTACK_OBSERVED;
    pub const ITEM_USE_OBSERVED: u32 = crate::sys::BBM_EV_ITEM_USE_OBSERVED;
}
/// Caller reached after a successful behavior action and its additional flag check.
pub const ATTACK_SOURCE_BEHAVIOR: u32 = 1;
/// Caller reached after the engine stamina update.
pub const ATTACK_SOURCE_STAMINA: u32 = 2;
pub const AVAILABLE: u32 = 1;
impl Cx {
    /// Bounded read-only lookup; rejects unsupported layout and unstable samples.
    pub fn equipment_snapshot(&self) -> Result<EquipmentSnapshot> {
        let h=unsafe { &*self.host };
        if !h.has_equipment() { return Err(Error::UNSUPPORTED); }
        let mut out=EquipmentSnapshot::default();
        check(unsafe { (h.equipment_snapshot)(self.me,&mut out) })?;
        Ok(out)
    }
}
impl Event<'_> {
    /// Native behavior observation from one of two verified callsites, queued.
    /// Not a weapon ID, damage event, cancellation point or exactly-once signal.
    pub fn attack_observed(&self) -> Option<AttackObserved> {
        if self.kind!=event::ATTACK_OBSERVED || self.data.len()!=core::mem::size_of::<AttackObserved>() {return None;}
        let v=unsafe {core::ptr::read_unaligned(self.data.as_ptr().cast::<AttackObserved>())};
        if v.size as usize!=core::mem::size_of::<AttackObserved>() || !(1..=2).contains(&v.source) {return None;}
        Some(v)
    }
    /// Actual native item-action result, delivered later at a safe sync point.
    /// Missing events are possible during overflow, loading or unsupported builds.
    pub fn item_use(&self) -> Option<ItemUse> {
        if self.kind!=event::ITEM_USE_OBSERVED || self.data.len()!=core::mem::size_of::<ItemUse>() {return None;}
        let v=unsafe {core::ptr::read_unaligned(self.data.as_ptr().cast::<ItemUse>())};
        if v.size as usize!=core::mem::size_of::<ItemUse>() || v.success>1 {return None;}
        Some(v)
    }
    /// Copies a sampled diagnostic payload. Does not imply a native equip/use action.
    pub fn equipment_change(&self) -> Option<EquipmentChange> {
        if self.kind!=event::EQUIPMENT_CHANGED || self.data.len()!=core::mem::size_of::<EquipmentChange>() {return None;}
        let v=unsafe {core::ptr::read_unaligned(self.data.as_ptr().cast::<EquipmentChange>())};
        if v.current.size as usize!=core::mem::size_of::<EquipmentSnapshot>() ||
            (v.previous.size!=0 && v.previous.size!=v.current.size) {return None;}
        Some(v)
    }
}
#[cfg(test)] mod tests {
    use super::*;
    #[test] fn attack_payload_validation() {
        assert_eq!(core::mem::size_of::<AttackObserved>(),32);
        let mut v=AttackObserved{size:32,source:ATTACK_SOURCE_BEHAVIOR,generation:7,actor:123,behavior_id:456,reserved:0};
        fn decode(v:&AttackObserved)->Option<AttackObserved>{
            let data=unsafe {core::slice::from_raw_parts((v as *const AttackObserved).cast::<u8>(),32)};
            Event{kind:event::ATTACK_OBSERVED,a:0,b:0,data}.attack_observed()
        }
        assert_eq!(decode(&v).unwrap().behavior_id,456);
        v.source=ATTACK_SOURCE_STAMINA;assert!(decode(&v).is_some());
        v.source=0;assert!(decode(&v).is_none());
        v.source=3;assert!(decode(&v).is_none());
        v.source=1;v.size=31;assert!(decode(&v).is_none());
        assert!(Event{kind:event::ATTACK_OBSERVED,a:0,b:0,data:&[]}.attack_observed().is_none());
    }
    #[test] fn layout_and_bad_event() {
        assert_eq!(core::mem::size_of::<EquipmentSnapshot>(),200);
        assert_eq!(core::mem::size_of::<EquipmentChange>(),400);
        assert!(Event{kind:event::EQUIPMENT_CHANGED,a:0,b:0,data:&[]}.equipment_change().is_none());
    }
}
