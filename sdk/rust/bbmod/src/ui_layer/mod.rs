//! Composable UI layer for HUDs and interactive screens.
//!
//! Mods build a retained `Node` tree (panels, rows/columns, text, icons, bars, buttons,
//! toggles, sliders, choices, lists, tabs, key guides) and either:
//!
//! * publish it to the host with `HostScreen` (ABI 1.11): the host draws it in the
//!   overlay, fades it at 60 Hz, scales the 1920x1080 stage to the game's frame
//!   rectangle, and routes pad/keyboard input to the foreground layer; or
//! * run it in-process with `UiLayers` (reference model and test harness).
//!
//! HUD layers are passive and never capture input; Screen and Modal layers do.
//! The host renderer draws with the overlay's font and placeholders for game
//! textures (`DrawCmd::Image`) until the asset pipeline exists; it does not yet use
//! the game's own font or art.

mod clock;
mod encode;
mod geom;
mod host;
mod layers;
mod node;
mod screen;

pub use clock::{Phase, Playback, Presence, UiClock, MAX_CATCH_UP, TICKS_PER_AUTHORED_FRAME, TICK_HZ};
pub use encode::{decode, encode, MAX_BYTES, MAX_COMMANDS};
pub use geom::{Canvas, Rect, STAGE_H, STAGE_W};
pub use host::{HostScreen, EVENTS};
pub use layers::{Anchor, Handle, LayerKind, UiEvent, UiLayers};
pub use node::{render_node, theme, Align, Color, DrawCmd, Estimated, Kind, Layout, ListData, ListRow, Measure, Node, Size};
pub use screen::{Action, EventKind, Screen};
