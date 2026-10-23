//! bb-modding installer: folder validation and payload installation. The GUI (`main.rs`) is a
//! thin shell over this library so everything here is testable without a window.
pub mod auto;
pub mod install;
pub mod payload;
pub mod validate;
