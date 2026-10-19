//! Shared gameplay service (ABI 1.4).
//!
//! PLAYER_CHANGED is a sampled HP/stamina/identity observation, not a damage,
//! attack, heal or stamina-recovery hook. Multiple changes between sync points
//! may coalesce. Modifiers affect only explicit `player_adjust` requests, both
//! positive and negative deltas; they do not intercept the game's own writes.
use crate::{check, sys, Cx, Error, Event, Id, Result};
pub use sys::bbm_player_snapshot as PlayerSnapshot;
pub use sys::bbm_player_change as PlayerChange;
pub mod event { pub const PLAYER_CHANGED: u32 = crate::sys::BBM_EV_PLAYER_CHANGED; }
pub const AVAILABLE: u32 = sys::BBM_PLAYER_AVAILABLE;
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum Resource { Health = sys::BBM_RESOURCE_HEALTH, Stamina = sys::BBM_RESOURCE_STAMINA }
impl Resource {
    pub fn value(self, snapshot: &PlayerSnapshot) -> i32 {
        match self { Self::Health => snapshot.health, Self::Stamina => snapshot.stamina }
    }
}
impl Event<'_> {
    /// Copies an integer-only typed payload; malformed event sizes are rejected.
    pub fn player_change(&self) -> Option<PlayerChange> {
        if self.kind != event::PLAYER_CHANGED || self.data.len() != core::mem::size_of::<PlayerChange>() { return None; }
        let change = unsafe { core::ptr::read_unaligned(self.data.as_ptr().cast::<PlayerChange>()) };
        if change.current.size as usize != core::mem::size_of::<PlayerSnapshot>() { return None; }
        if change.previous.size != 0 && change.previous.size != change.current.size { return None; }
        Some(change)
    }
}
impl Cx {
    /// Fast bounded layout lookup. Fails closed on unsupported image or unavailable player.
    /// The actor/component fields are diagnostic identities, not permission to dereference.
    pub fn player_snapshot(&self) -> Result<PlayerSnapshot> {
        let h = unsafe { &*self.host };
        if !h.has_gameplay() { return Err(Error::UNSUPPORTED); }
        let mut out = PlayerSnapshot::default();
        check(unsafe { (h.player_snapshot)(self.me, &mut out) })?;
        Ok(out)
    }
    /// Apply an explicit delta from a game-thread event/frame callback. UI should
    /// queue requests to on_frame. Refuses stale identity/value, death and network
    /// sessions. Scales compose then HP clamps to 1..max, stamina to 0..max.
    /// No automatic undo: the game may already have consumed the new value.
    /// This is an optimistic checked write, not atomic against game worker threads.
    pub fn player_adjust(&self, resource: Resource, expected: &PlayerSnapshot, delta: i32) -> Result<i32> {
        let h = unsafe { &*self.host };
        if !h.has_gameplay() { return Err(Error::UNSUPPORTED); }
        let mut actual = 0;
        check(unsafe { (h.player_adjust)(self.me, resource as u32, expected.generation, resource.value(expected), delta, &mut actual) })?;
        Ok(actual)
    }
    /// Owned scale claim, 0..4000 permille. Ascending priority, mod id and resource
    /// id order; integer rounding toward zero after each scale. At most 64 active
    /// claims host-wide. Applies to positive AND negative explicit request deltas.
    /// Release with cx.release(id); unload/fault excludes the claim automatically.
    pub fn player_modifier(&self, resource: Resource, scale_milli: u32, priority: i32) -> Result<Id> {
        let h = unsafe { &*self.host };
        if !h.has_gameplay() { return Err(Error::UNSUPPORTED); }
        let mut id = 0;
        check(unsafe { (h.player_modifier)(self.me, resource as u32, scale_milli, priority, &mut id) })?;
        Ok(Id(id))
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test] fn c_layout() {
        assert_eq!(core::mem::size_of::<PlayerSnapshot>(),48);
        assert_eq!(core::mem::size_of::<PlayerChange>(),96);
    }
    #[test] fn rejects_wrong_and_short_event_payloads() {
        let e=Event {kind:event::PLAYER_CHANGED,a:0,b:0,data:&[]};
        assert!(e.player_change().is_none());
        let p=PlayerChange {previous:PlayerSnapshot::default(),current:PlayerSnapshot {size:core::mem::size_of::<PlayerSnapshot>() as u32,flags:AVAILABLE,health:573,..Default::default()}};
        let bytes=unsafe {core::slice::from_raw_parts((&p as *const PlayerChange).cast::<u8>(),core::mem::size_of::<PlayerChange>())};
        let e=Event {kind:event::PLAYER_CHANGED,a:0,b:0,data:bytes};
        assert_eq!(e.player_change().unwrap().current.health,573);
        assert!(Event {kind:0,..e}.player_change().is_none());
    }
}
