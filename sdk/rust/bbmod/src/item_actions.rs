//! Bounded item awards executed on a verified native player-update callback.
//! These can persist in normal game saves. Submission is not proof of inventory
//! insertion; observe inventory afterwards. Pending work expires after 5 seconds.
use crate::{check,sys,Cx,Error,Result};
#[derive(Debug,Clone,Copy,PartialEq,Eq)]
pub struct ItemGiveRequest(pub u64);
#[derive(Debug,Clone,Copy,PartialEq,Eq)]
pub enum ItemGiveState { Pending,Running,Submitted,Cancelled,Failed }
#[derive(Debug,Clone,Copy)]
pub struct ItemGiveResult { pub request:ItemGiveRequest,pub state:ItemGiveState,pub generation:u64,pub encoded_item_id:u32,pub quantity:u32 }
impl Cx {
    /// Queue a catalog item already present in the active game data. No assets
    /// or PARAM rows are installed by this call. Goods allow 1..99, equipment 1.
    /// Maximum 8 unconsumed requests per mod, 32 total. A running native award
    /// cannot be cancelled; pending requests are cancelled on unload/fault.
    pub fn give_item_raw(&self,encoded:u32,quantity:u32,generation:u64)->Result<ItemGiveRequest>{
        let h=unsafe{&*self.host};if !h.has_item_actions(){return Err(Error::UNSUPPORTED);}
        let mut id=0;check(unsafe{(h.item_give)(self.me,encoded,quantity,generation,&mut id)})?;
        Ok(ItemGiveRequest(id))
    }
    /// Terminal results are consumed by this query; later queries return NOTFOUND.
    /// Submitted means construct/award/release ran, not that a save was committed.
    pub fn item_give_status(&self,request:ItemGiveRequest)->Result<ItemGiveResult>{
        let h=unsafe{&*self.host};if !h.has_item_actions(){return Err(Error::UNSUPPORTED);}
        let mut v=sys::bbm_item_give_result::default();check(unsafe{(h.item_give_status)(self.me,request.0,&mut v)})?;
        let state=match v.state {sys::BBM_ITEM_PENDING=>ItemGiveState::Pending,sys::BBM_ITEM_RUNNING=>ItemGiveState::Running,sys::BBM_ITEM_SUBMITTED=>ItemGiveState::Submitted,sys::BBM_ITEM_CANCELLED=>ItemGiveState::Cancelled,sys::BBM_ITEM_FAILED=>ItemGiveState::Failed,_=>return Err(Error::STATE)};
        Ok(ItemGiveResult{request:ItemGiveRequest(v.request_id),state,generation:v.generation,encoded_item_id:v.encoded_item_id,quantity:v.quantity})
    }
}
