//! Safe Rust SDK for bbport mods.
//!
//! ```ignore
//! struct MyMod { frames: u64 }
//! impl bbmod::Mod for MyMod {
//!     fn load(cx: &mut bbmod::Cx) -> bbmod::Result<Self> {
//!         cx.log_info("hello");
//!         Ok(MyMod { frames: 0 })
//!     }
//! }
//! bbmod::export!(MyMod);
//! ```
//!
//! Rules the SDK enforces for you:
//! * every host resource is owned by the host and released on unload;
//! * panics never cross the FFI boundary (they are logged, the call fails);
//! * closures you hand to the host are dropped only after the host has
//!   released every callback that could call them.
//!
//! Rules you must follow (see docs/RUST_SDK.md): no threads of your own
//! (use `Cx::spawn`), no TLS destructors, no `static` that holds host handles.

mod cx;
mod detour;
mod entry;
pub mod key;
pub mod dualsense;
pub mod gameplay;
pub mod characters;
pub mod character_definitions;
pub mod character_references;
pub mod inventory;
pub mod items;
pub mod item_actions;
pub mod content;
pub mod equipment;
pub mod native_menu;
mod ui;

pub use bbmod_sys as sys;
pub use character_definitions::{get_character_definition,CharacterDefinition,NpcParamId};
pub use cx::{Channel, Cx, Event, Id, Reader, SessionInfo};
pub use detour::{Detour, InflightGuard, __detour_panicked};
pub use entry::{Mod, __internal};
pub use ui::{Ui,Table};

/// A read-only observation from the host's single controller sampling point.
/// Delivery uses the normal serialized Mod::event callback. Observers must not
/// depend on their relative dispatch order; this event cannot modify game input.
#[derive(Debug, Clone, Copy)]
pub struct PadChange {
    pub current: u32,
    pub previous: u32,
}
impl PadChange {
    pub fn from_event(event: &Event<'_>) -> Option<Self> {
        (event.kind == sys::BBM_EV_PAD_CHANGED).then_some(Self {
            current: event.a as u32, previous: event.b as u32,
        })
    }
    pub fn pressed(self) -> u32 { self.current & !self.previous }
    pub fn released(self) -> u32 { self.previous & !self.current }
}

pub use sys::bbm_version as Version;

/// A host status code other than `BBM_OK`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Error(pub i32);

impl Error {
    pub const GENERIC: Error = Error(sys::BBM_ERR);
    pub const ARG: Error = Error(sys::BBM_ERR_ARG);
    pub const CONFLICT: Error = Error(sys::BBM_ERR_CONFLICT);
    pub const UNSUPPORTED: Error = Error(sys::BBM_ERR_UNSUPPORTED);
    pub const STATE: Error = Error(sys::BBM_ERR_STATE);
    pub const NOT_FOUND: Error = Error(sys::BBM_ERR_NOTFOUND);
    pub const BUDGET: Error = Error(sys::BBM_ERR_BUDGET);
}

impl core::fmt::Display for Error {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        let what = match self.0 {
            sys::BBM_ERR_ARG => "bad argument",
            sys::BBM_ERR_CONFLICT => "conflicts with another mod",
            sys::BBM_ERR_UNSUPPORTED => "not supported by this host",
            sys::BBM_ERR_NOMEM => "out of memory",
            sys::BBM_ERR_STATE => "not allowed right now",
            sys::BBM_ERR_NOTFOUND => "not found",
            sys::BBM_ERR_BUDGET => "network budget exceeded",
            _ => "host error",
        };
        write!(f, "{what} (code {})", self.0)
    }
}

impl std::error::Error for Error {}

pub type Result<T> = core::result::Result<T, Error>;

pub(crate) fn check(rc: i32) -> Result<()> {
    if rc == sys::BBM_OK { Ok(()) } else { Err(Error(rc)) }
}

pub mod log_level {
    pub const DEBUG: i32 = super::sys::BBM_LOG_DEBUG;
    pub const INFO: i32 = super::sys::BBM_LOG_INFO;
    pub const WARN: i32 = super::sys::BBM_LOG_WARN;
    pub const ERROR: i32 = super::sys::BBM_LOG_ERROR;
}

/// Pad button bits as the game sees them, for `Cx::pad_down` / `pad_pressed`.
pub mod pad {
    pub use super::sys::{
        BBM_PAD_CIRCLE as CIRCLE, BBM_PAD_CROSS as CROSS, BBM_PAD_DOWN as DOWN, BBM_PAD_L1 as L1,
        BBM_PAD_L2 as L2, BBM_PAD_L3 as L3, BBM_PAD_LEFT as LEFT, BBM_PAD_OPTIONS as OPTIONS,
        BBM_PAD_R1 as R1, BBM_PAD_R2 as R2, BBM_PAD_R3 as R3, BBM_PAD_RIGHT as RIGHT,
        BBM_PAD_SQUARE as SQUARE, BBM_PAD_TOUCHPAD as TOUCHPAD, BBM_PAD_TRIANGLE as TRIANGLE,
        BBM_PAD_UP as UP,
    };
}

/// `bbmod::info!(cx, "hp = {}", hp)` and friends: formatted logging.
#[macro_export]
macro_rules! info { ($cx:expr, $($t:tt)*) => { $cx.log_info(&format!($($t)*)) }; }
#[macro_export]
macro_rules! warn { ($cx:expr, $($t:tt)*) => { $cx.log_warn(&format!($($t)*)) }; }
#[macro_export]
macro_rules! error { ($cx:expr, $($t:tt)*) => { $cx.log_error(&format!($($t)*)) }; }
#[macro_export]
macro_rules! debug { ($cx:expr, $($t:tt)*) => { $cx.log_debug(&format!($($t)*)) }; }
