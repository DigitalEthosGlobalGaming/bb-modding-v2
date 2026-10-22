//! Mod settings (ABI 1.11).
//!
//! A mod declares a schema; the host keeps the values (persisted per mod in
//! `config/<mod-id>.settings.toml`), validates every write against the declaration,
//! and tells the owner with `Event::setting_changed`. A mod manager (a mod whose
//! manifest has `manage = true`) reads schemas and writes values through `Manager`.
//!
//! ```ignore
//! fn load(cx: &mut Cx) -> Result<Self> {
//!     cx.subscribe(bbmod::settings::EVENTS)?;
//!     cx.declare_setting(&Def::int("damage", "Damage taken %", 20, 500, 100).step(5.0).restart(false))?;
//!     let damage = cx.setting("damage")?.as_int().unwrap();
//! }
//! fn event(&mut self, cx: &mut Cx, ev: &Event) {
//!     if let Some(change) = ev.setting_changed() { /* change.key, change.value */ }
//! }
//! ```

use crate::{check, sys, Cx, Error, Event, Result};
use core::ffi::c_char;

/// Subscribe with this (OR'd with other masks) to receive `Event::setting_changed`.
pub const EVENTS: u32 = sys::BBM_EV_SETTING_CHANGED;

#[derive(Clone, Debug, PartialEq)]
pub enum Kind {
    Bool { default: bool },
    Int { min: i64, max: i64, step: i64, default: i64 },
    Float { min: f64, max: f64, step: f64, default: f64 },
    Enum { choices: Vec<String>, default: usize },
    /// UTF-8 text of at most `max_len` bytes (1..=95).
    Text { max_len: usize, default: String },
    /// An SDL scancode (see `bbmod::key`).
    Keybind { default: i32 },
}

#[derive(Clone, Debug, PartialEq)]
pub enum Value { Bool(bool), Int(i64), Float(f64), Enum(usize), Text(String), Keybind(i32) }

impl Value {
    pub fn as_bool(&self) -> Option<bool> { if let Value::Bool(v) = self { Some(*v) } else { None } }
    pub fn as_int(&self) -> Option<i64> { if let Value::Int(v) = self { Some(*v) } else { None } }
    pub fn as_float(&self) -> Option<f64> { if let Value::Float(v) = self { Some(*v) } else { None } }
    pub fn as_enum(&self) -> Option<usize> { if let Value::Enum(v) = self { Some(*v) } else { None } }
    pub fn as_text(&self) -> Option<&str> { if let Value::Text(v) = self { Some(v) } else { None } }
    pub fn as_key(&self) -> Option<i32> { if let Value::Keybind(v) = self { Some(*v) } else { None } }
}

/// A declared setting. Build with the typed constructors and the chainable setters.
#[derive(Clone, Debug, PartialEq)]
pub struct Def {
    pub key: String,
    pub label: String,
    pub help: String,
    pub group: String,
    pub kind: Kind,
    pub order: i32,
    /// Applies only after a restart (the menu says so).
    pub restart: bool,
    /// Shown only while the integer-like value of this setting equals the number.
    pub depends: Option<(String, i32)>,
}

impl Def {
    fn new(key: &str, label: &str, kind: Kind) -> Self {
        Self { key: key.into(), label: label.into(), help: String::new(), group: String::new(), kind, order: 0, restart: false, depends: None }
    }
    pub fn bool(key: &str, label: &str, default: bool) -> Self { Self::new(key, label, Kind::Bool { default }) }
    pub fn int(key: &str, label: &str, min: i64, max: i64, default: i64) -> Self {
        Self::new(key, label, Kind::Int { min, max, step: 1, default })
    }
    pub fn float(key: &str, label: &str, min: f64, max: f64, default: f64) -> Self {
        Self::new(key, label, Kind::Float { min, max, step: (max - min) / 20.0, default })
    }
    pub fn choice(key: &str, label: &str, choices: &[&str], default: usize) -> Self {
        Self::new(key, label, Kind::Enum { choices: choices.iter().map(|s| s.to_string()).collect(), default })
    }
    pub fn text(key: &str, label: &str, max_len: usize, default: &str) -> Self {
        Self::new(key, label, Kind::Text { max_len, default: default.into() })
    }
    pub fn keybind(key: &str, label: &str, default: i32) -> Self { Self::new(key, label, Kind::Keybind { default }) }
    pub fn step(mut self, s: f64) -> Self {
        match &mut self.kind { Kind::Int { step, .. } => *step = s.max(1.0) as i64, Kind::Float { step, .. } => *step = s, _ => {} }
        self
    }
    pub fn help(mut self, h: &str) -> Self { self.help = h.into(); self }
    pub fn group(mut self, g: &str) -> Self { self.group = g.into(); self }
    pub fn order(mut self, o: i32) -> Self { self.order = o; self }
    pub fn restart(mut self, r: bool) -> Self { self.restart = r; self }
    pub fn depends_on(mut self, key: &str, value: i32) -> Self { self.depends = Some((key.into(), value)); self }

    /// Whether the host's fixed-size declaration can hold this definition (key, label, help,
    /// group, choices and text within their limits). The host also checks ranges and defaults.
    pub fn validate(&self) -> Result<()> { self.raw().map(|_| ()) }

    pub fn default_value(&self) -> Value {
        match &self.kind {
            Kind::Bool { default } => Value::Bool(*default),
            Kind::Int { default, .. } => Value::Int(*default),
            Kind::Float { default, .. } => Value::Float(*default),
            Kind::Enum { default, .. } => Value::Enum(*default),
            Kind::Text { default, .. } => Value::Text(default.clone()),
            Kind::Keybind { default } => Value::Keybind(*default),
        }
    }
}

fn put(text: &str, out: &mut [c_char]) -> Result<()> {
    if text.len() >= out.len() || text.bytes().any(|b| b == 0) { return Err(Error::ARG); }
    for (a, b) in out.iter_mut().zip(text.bytes()) { *a = b as c_char; }
    Ok(())
}
fn get(s: &[c_char]) -> String {
    let end = s.iter().position(|c| *c == 0).unwrap_or(s.len());
    String::from_utf8_lossy(&s.iter().take(end).map(|c| *c as u8).collect::<Vec<_>>()).into_owned()
}

impl Def {
    pub(crate) fn raw(&self) -> Result<sys::bbm_setting_def> {
        let mut d = sys::bbm_setting_def { size: core::mem::size_of::<sys::bbm_setting_def>() as u32, order: self.order,
            flags: if self.restart { sys::BBM_SET_RESTART } else { 0 }, ..Default::default() };
        put(&self.key, &mut d.key)?;
        put(&self.label, &mut d.label)?;
        put(&self.help, &mut d.help)?;
        put(&self.group, &mut d.group)?;
        if let Some((k, v)) = &self.depends { put(k, &mut d.depends_key)?; d.depends_value = *v; }
        match &self.kind {
            Kind::Bool { default } => { d.type_ = sys::BBM_SET_BOOL; d.def_i = *default as i64; }
            Kind::Int { min, max, step, default } => {
                d.type_ = sys::BBM_SET_INT; d.min = *min as f64; d.max = *max as f64; d.step = *step as f64; d.def_i = *default;
            }
            Kind::Float { min, max, step, default } => {
                d.type_ = sys::BBM_SET_FLOAT; d.min = *min; d.max = *max; d.step = *step; d.def_f = *default;
            }
            Kind::Enum { choices, default } => {
                d.type_ = sys::BBM_SET_ENUM; d.def_i = *default as i64;
                if choices.iter().any(|c| c.contains('|')) { return Err(Error::ARG); }
                put(&choices.join("|"), &mut d.choices)?;
            }
            Kind::Text { max_len, default } => {
                d.type_ = sys::BBM_SET_TEXT; d.max = *max_len as f64; put(default, &mut d.def_s)?;
            }
            Kind::Keybind { default } => { d.type_ = sys::BBM_SET_KEYBIND; d.def_i = *default as i64; }
        }
        Ok(d)
    }
    pub(crate) fn from_raw(d: &sys::bbm_setting_def) -> Option<(Def, bool)> {
        let kind = match d.type_ {
            sys::BBM_SET_BOOL => Kind::Bool { default: d.def_i != 0 },
            sys::BBM_SET_INT => Kind::Int { min: d.min as i64, max: d.max as i64, step: (d.step as i64).max(1), default: d.def_i },
            sys::BBM_SET_FLOAT => Kind::Float { min: d.min, max: d.max, step: d.step, default: d.def_f },
            sys::BBM_SET_ENUM => Kind::Enum { choices: get(&d.choices).split('|').map(str::to_owned).collect(), default: d.def_i.max(0) as usize },
            sys::BBM_SET_TEXT => Kind::Text { max_len: d.max as usize, default: get(&d.def_s) },
            sys::BBM_SET_KEYBIND => Kind::Keybind { default: d.def_i as i32 },
            _ => return None,
        };
        let key = get(&d.depends_key);
        Some((Def { key: get(&d.key), label: get(&d.label), help: get(&d.help), group: get(&d.group), kind, order: d.order,
            restart: d.flags & sys::BBM_SET_RESTART != 0, depends: (!key.is_empty()).then_some((key, d.depends_value)) },
            d.flags & sys::BBM_SET_INACTIVE != 0))
    }
}

impl Value {
    pub(crate) fn raw(&self) -> Result<sys::bbm_setting_value> {
        let mut v = sys::bbm_setting_value { size: core::mem::size_of::<sys::bbm_setting_value>() as u32, ..Default::default() };
        match self {
            Value::Bool(b) => { v.type_ = sys::BBM_SET_BOOL; v.i = *b as i64; }
            Value::Int(i) => { v.type_ = sys::BBM_SET_INT; v.i = *i; }
            Value::Float(f) => { v.type_ = sys::BBM_SET_FLOAT; v.f = *f; }
            Value::Enum(i) => { v.type_ = sys::BBM_SET_ENUM; v.i = *i as i64; }
            Value::Text(s) => { v.type_ = sys::BBM_SET_TEXT; put(s, &mut v.s)?; }
            Value::Keybind(k) => { v.type_ = sys::BBM_SET_KEYBIND; v.i = *k as i64; }
        }
        Ok(v)
    }
    pub(crate) fn from_raw(v: &sys::bbm_setting_value) -> Option<Value> {
        Some(match v.type_ {
            sys::BBM_SET_BOOL => Value::Bool(v.i != 0),
            sys::BBM_SET_INT => Value::Int(v.i),
            sys::BBM_SET_FLOAT => Value::Float(v.f),
            sys::BBM_SET_ENUM => Value::Enum(v.i.max(0) as usize),
            sys::BBM_SET_TEXT => Value::Text(get(&v.s)),
            sys::BBM_SET_KEYBIND => Value::Keybind(v.i as i32),
            _ => return None,
        })
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Change { pub key: String, pub value: Value }

impl Event<'_> {
    /// The host changed one of this mod's settings (a manager edited it). The value is
    /// already stored and validated; apply it.
    pub fn setting_changed(&self) -> Option<Change> {
        if self.kind != sys::BBM_EV_SETTING_CHANGED || self.data.len() != core::mem::size_of::<sys::bbm_setting_event>() { return None; }
        // SAFETY: length checked; unaligned read of a plain C struct.
        let raw = unsafe { core::ptr::read_unaligned(self.data.as_ptr().cast::<sys::bbm_setting_event>()) };
        if raw.size as usize != self.data.len() { return None; }
        Some(Change { key: get(&raw.key), value: Value::from_raw(&raw.value)? })
    }
}

impl Cx {
    /// Declare a setting. The host loads a saved value, if any, over the default.
    /// Re-declaring a live key is a conflict; declarations are released with the mod.
    pub fn declare_setting(&mut self, def: &Def) -> Result<()> {
        let h = unsafe { &*self.host };
        if !h.has_settings() { return Err(Error::UNSUPPORTED); }
        let raw = def.raw()?;
        check(unsafe { (h.setting_declare)(self.me, &raw) })
    }
    /// The current (host-validated) value.
    pub fn setting(&self, key: &str) -> Result<Value> {
        let h = unsafe { &*self.host };
        if !h.has_settings() { return Err(Error::UNSUPPORTED); }
        let k = std::ffi::CString::new(key).map_err(|_| Error::ARG)?;
        let mut v = sys::bbm_setting_value { size: core::mem::size_of::<sys::bbm_setting_value>() as u32, ..Default::default() };
        check(unsafe { (h.setting_get)(self.me, k.as_ptr(), &mut v) })?;
        Value::from_raw(&v).ok_or(Error::STATE)
    }
    /// Change one of your own settings (validated and persisted; no event comes back to you).
    pub fn set_setting(&mut self, key: &str, value: &Value) -> Result<()> {
        let h = unsafe { &*self.host };
        if !h.has_settings() { return Err(Error::UNSUPPORTED); }
        let k = std::ffi::CString::new(key).map_err(|_| Error::ARG)?;
        let raw = value.raw()?;
        check(unsafe { (h.setting_set)(self.me, k.as_ptr(), &raw) })
    }
}

/// Mod state as the manager sees it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ModState { Discovered, Active, Unloaded, Faulted, Disabled }

#[derive(Clone, Debug, PartialEq)]
pub struct ModInfo {
    pub id: String,
    pub name: String,
    pub version: (u16, u16, u16),
    pub author: String,
    pub description: String,
    pub last_error: String,
    /// Mods this one asks to load after (ordering only, not a hard dependency).
    pub after: String,
    pub conflicts: String,
    pub state: ModState,
    pub has_code: bool,
    pub hot_reload: bool,
    /// This is the calling manager itself; it cannot unload itself.
    pub is_self: bool,
    pub has_settings: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ModOp { Load, Unload, Reload }

/// One declared setting of a mod, with its current value.
#[derive(Clone, Debug, PartialEq)]
pub struct Entry { pub def: Def, pub value: Value, /// The owner is not loaded; edits apply when it loads again.
    pub inactive: bool }

impl Cx {
    fn manage(&self) -> Result<&sys::bbm_host> {
        let h = unsafe { &*self.host };
        if h.has_manage() { Ok(h) } else { Err(Error::UNSUPPORTED) }
    }
    /// Every mod the host knows, in load order. Needs `[mod] manage = true`.
    pub fn manager_mods(&self) -> Result<Vec<ModInfo>> {
        let h = self.manage()?;
        let mut out = Vec::new();
        for i in 0u32.. {
            let mut r = sys::bbm_mod_info { size: core::mem::size_of::<sys::bbm_mod_info>() as u32, ..Default::default() };
            match unsafe { (h.mods_info)(self.me, i, &mut r) } {
                sys::BBM_OK => {}
                sys::BBM_ERR_NOTFOUND => break,
                rc => return Err(Error(rc)),
            }
            out.push(ModInfo { id: get(&r.id), name: get(&r.name), version: (r.version.major, r.version.minor, r.version.patch),
                author: get(&r.author), description: get(&r.description), last_error: get(&r.last_error),
                after: get(&r.after), conflicts: get(&r.conflicts),
                state: match r.state { 1 => ModState::Active, 2 => ModState::Unloaded, 3 => ModState::Faulted, 4 => ModState::Disabled, _ => ModState::Discovered },
                has_code: r.flags & sys::BBM_MODF_HAS_CODE != 0, hot_reload: r.flags & sys::BBM_MODF_HOT_RELOAD != 0,
                is_self: r.flags & sys::BBM_MODF_SELF != 0, has_settings: r.flags & sys::BBM_MODF_SETTINGS != 0 });
            if out.len() >= 512 { break; }
        }
        Ok(out)
    }
    /// Queue a load, unload or reload for the next sync point.
    pub fn manager_request(&self, id: &str, op: ModOp) -> Result<()> {
        let h = self.manage()?;
        let k = std::ffi::CString::new(id).map_err(|_| Error::ARG)?;
        let op = match op { ModOp::Load => sys::BBM_MODOP_LOAD, ModOp::Unload => sys::BBM_MODOP_UNLOAD, ModOp::Reload => sys::BBM_MODOP_RELOAD };
        check(unsafe { (h.mod_request)(self.me, k.as_ptr(), op) })
    }
    /// A mod's declared settings (including those of an unloaded mod, marked inactive).
    pub fn manager_settings(&self, id: &str) -> Result<Vec<Entry>> {
        let h = self.manage()?;
        let k = std::ffi::CString::new(id).map_err(|_| Error::ARG)?;
        let mut out = Vec::new();
        for i in 0u32.. {
            let mut d = sys::bbm_setting_def { size: core::mem::size_of::<sys::bbm_setting_def>() as u32, ..Default::default() };
            let mut v = sys::bbm_setting_value { size: core::mem::size_of::<sys::bbm_setting_value>() as u32, ..Default::default() };
            match unsafe { (h.settings_info)(self.me, k.as_ptr(), i, &mut d, &mut v) } {
                sys::BBM_OK => {}
                sys::BBM_ERR_NOTFOUND => break,
                rc => return Err(Error(rc)),
            }
            if let (Some((def, inactive)), Some(value)) = (Def::from_raw(&d), Value::from_raw(&v)) { out.push(Entry { def, value, inactive }); }
            if out.len() >= 64 { break; }
        }
        out.sort_by(|a, b| a.def.order.cmp(&b.def.order));
        Ok(out)
    }
    /// Write another mod's setting. The host validates it, saves it and notifies the owner.
    pub fn manager_write_setting(&self, id: &str, key: &str, value: &Value) -> Result<()> {
        let h = self.manage()?;
        let (i, k) = (std::ffi::CString::new(id).map_err(|_| Error::ARG)?, std::ffi::CString::new(key).map_err(|_| Error::ARG)?);
        let raw = value.raw()?;
        check(unsafe { (h.settings_write)(self.me, i.as_ptr(), k.as_ptr(), &raw) })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn layouts_match_the_c_header() {
        assert_eq!(core::mem::size_of::<sys::bbm_setting_def>(), 800);
        assert_eq!(core::mem::size_of::<sys::bbm_setting_value>(), 120);
        assert_eq!(core::mem::size_of::<sys::bbm_setting_event>(), 176);
        assert_eq!(core::mem::size_of::<sys::bbm_mod_info>(), 1144);
    }
    #[test]
    fn definitions_and_values_roundtrip_through_the_c_structs() {
        for def in [
            Def::bool("b", "B", true).help("help").group("G").order(3).restart(true),
            Def::int("i", "I", -5, 50, 7).step(5.0).depends_on("b", 1),
            Def::float("f", "F", 0.5, 2.0, 1.0).step(0.25),
            Def::choice("e", "E", &["A", "B", "C"], 2),
            Def::text("t", "T", 16, "hi"),
            Def::keybind("k", "K", 59),
        ] {
            let raw = def.raw().unwrap();
            let (back, inactive) = Def::from_raw(&raw).unwrap();
            assert_eq!((back, inactive), (def.clone(), false));
            let v = def.default_value();
            assert_eq!(Value::from_raw(&v.raw().unwrap()), Some(v));
        }
        assert!(Def::choice("e", "E", &["A|B", "C"], 0).raw().is_err());
        assert!(Def::bool(&"k".repeat(48), "x", false).raw().is_err());
        assert!(Value::Text("x".repeat(96)).raw().is_err());
    }
    #[test]
    fn change_events_are_validated() {
        let mut raw = sys::bbm_setting_event { size: core::mem::size_of::<sys::bbm_setting_event>() as u32, ..Default::default() };
        put("level", &mut raw.key).unwrap();
        raw.value = Value::Int(12).raw().unwrap();
        let bytes = unsafe { core::slice::from_raw_parts((&raw as *const sys::bbm_setting_event).cast::<u8>(), core::mem::size_of_val(&raw)) };
        let ev = Event { kind: sys::BBM_EV_SETTING_CHANGED, a: 0, b: 0, data: bytes };
        assert_eq!(ev.setting_changed(), Some(Change { key: "level".into(), value: Value::Int(12) }));
        assert!(Event { kind: sys::BBM_EV_SETTING_CHANGED, a: 0, b: 0, data: &bytes[..10] }.setting_changed().is_none());
        assert!(Event { kind: 1, a: 0, b: 0, data: bytes }.setting_changed().is_none());
    }
}
