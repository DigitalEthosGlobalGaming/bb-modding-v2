//! Keyboard keys as SDL scancodes (USB HID usage ids), for `Cx::key_down` /
//! `Cx::key_pressed`. Insert opens bbport's own menu, so avoid it. While that
//! menu or a text box has focus, every key reads as released.
#![allow(missing_docs)]

pub const A: i32 = 4; pub const B: i32 = 5; pub const C: i32 = 6; pub const D: i32 = 7;
pub const E: i32 = 8; pub const F: i32 = 9; pub const G: i32 = 10; pub const H: i32 = 11;
pub const I: i32 = 12; pub const J: i32 = 13; pub const K: i32 = 14; pub const L: i32 = 15;
pub const M: i32 = 16; pub const N: i32 = 17; pub const O: i32 = 18; pub const P: i32 = 19;
pub const Q: i32 = 20; pub const R: i32 = 21; pub const S: i32 = 22; pub const T: i32 = 23;
pub const U: i32 = 24; pub const V: i32 = 25; pub const W: i32 = 26; pub const X: i32 = 27;
pub const Y: i32 = 28; pub const Z: i32 = 29;
pub const NUM1: i32 = 30; pub const NUM2: i32 = 31; pub const NUM3: i32 = 32; pub const NUM4: i32 = 33;
pub const NUM5: i32 = 34; pub const NUM6: i32 = 35; pub const NUM7: i32 = 36; pub const NUM8: i32 = 37;
pub const NUM9: i32 = 38; pub const NUM0: i32 = 39;
pub const ENTER: i32 = 40; pub const ESCAPE: i32 = 41; pub const BACKSPACE: i32 = 42; pub const TAB: i32 = 43;
pub const SPACE: i32 = 44;
pub const F1: i32 = 58; pub const F2: i32 = 59; pub const F3: i32 = 60; pub const F4: i32 = 61;
pub const F5: i32 = 62; pub const F6: i32 = 63; pub const F7: i32 = 64; pub const F8: i32 = 65;
pub const F9: i32 = 66; pub const F10: i32 = 67; pub const F11: i32 = 68; pub const F12: i32 = 69;
pub const INSERT: i32 = 73; pub const HOME: i32 = 74; pub const PAGE_UP: i32 = 75; pub const DELETE: i32 = 76;
pub const END: i32 = 77; pub const PAGE_DOWN: i32 = 78;
pub const RIGHT: i32 = 79; pub const LEFT: i32 = 80; pub const DOWN: i32 = 81; pub const UP: i32 = 82;
pub const KP1: i32 = 89; pub const KP2: i32 = 90; pub const KP3: i32 = 91; pub const KP4: i32 = 92;
pub const KP5: i32 = 93; pub const KP6: i32 = 94; pub const KP7: i32 = 95; pub const KP8: i32 = 96;
pub const KP9: i32 = 97; pub const KP0: i32 = 98;
pub const LCTRL: i32 = 224; pub const LSHIFT: i32 = 225; pub const LALT: i32 = 226;
pub const RCTRL: i32 = 228; pub const RSHIFT: i32 = 229; pub const RALT: i32 = 230;

/// Keys offered by pickers (menus that cannot capture a raw key press), with display names.
pub const PICKER: &[(i32, &str)] = &[
    (F1, "F1"), (F2, "F2"), (F3, "F3"), (F4, "F4"), (F5, "F5"), (F6, "F6"),
    (F7, "F7"), (F8, "F8"), (F9, "F9"), (F10, "F10"), (F11, "F11"), (F12, "F12"),
    (HOME, "Home"), (END, "End"), (PAGE_UP, "Page Up"), (PAGE_DOWN, "Page Down"), (DELETE, "Delete"),
    (KP1, "Keypad 1"), (KP2, "Keypad 2"), (KP3, "Keypad 3"), (KP4, "Keypad 4"), (KP5, "Keypad 5"),
    (KP6, "Keypad 6"), (KP7, "Keypad 7"), (KP8, "Keypad 8"), (KP9, "Keypad 9"), (KP0, "Keypad 0"),
    (A, "A"), (B, "B"), (C, "C"), (D, "D"), (E, "E"), (F, "F"), (G, "G"), (H, "H"), (I, "I"), (J, "J"),
    (K, "K"), (L, "L"), (M, "M"), (N, "N"), (O, "O"), (P, "P"), (Q, "Q"), (R, "R"), (S, "S"), (T, "T"),
    (U, "U"), (V, "V"), (W, "W"), (X, "X"), (Y, "Y"), (Z, "Z"),
    (NUM1, "1"), (NUM2, "2"), (NUM3, "3"), (NUM4, "4"), (NUM5, "5"), (NUM6, "6"), (NUM7, "7"), (NUM8, "8"), (NUM9, "9"), (NUM0, "0"),
];

/// Display name for a scancode, "Key <n>" when it is not in the picker list.
pub fn name(scancode: i32) -> String {
    PICKER.iter().find(|(k, _)| *k == scancode).map_or_else(|| format!("Key {scancode}"), |(_, n)| (*n).to_string())
}
