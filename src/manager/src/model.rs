//! Menu state and behaviour, independent of the host. `Model` turns widget events into
//! `Command`s; `lib.rs` executes them against the host and feeds snapshots back.

use bbmod::settings::{Def, Entry, Kind, ModInfo, ModOp, ModState, Value};
use bbmod::ui_layer::EventKind;

pub const TAB_MODS: usize = 0;
pub const TAB_SETTINGS: usize = 1;
/// Widget key prefix for generated setting controls; the rest is the setting key.
pub const CONTROL: &str = "s:";

#[derive(Clone, Debug, PartialEq)]
pub enum Command {
    Close,
    Request { id: String, op: ModOp },
    Write { id: String, key: String, value: Value },
    /// Edit text in the system dialog, then `Write` the result.
    EditText { id: String, key: String, initial: String, max_bytes: usize },
    /// The settings shown changed: fetch `manager_settings(id)`.
    LoadSettings(String),
    FocusControls,
    FocusList,
    /// Move focus into the actions beside the mods list (Load, Reload, Uninstall).
    FocusDetail,
    FocusMods,
    /// Remove an installed mod (unloads it first).
    Uninstall(String),
}

#[derive(Default)]
pub struct Model {
    pub tab: usize,
    pub mods: Vec<ModInfo>,
    pub selected_mod: Option<String>,
    pub settings_mod: Option<String>,
    pub entries: Vec<Entry>,
    pub status: String,
    /// True while the keyboard/pad focus is in the settings controls rather than the list.
    pub in_controls: bool,
    /// True while focus is in the actions beside the mods list rather than the list.
    pub in_detail: bool,
    /// The load / unload / reload just requested, until the host reports it done.
    pending_op: Option<(String, ModOp, u8)>,
}

pub fn state_mark(s: ModState) -> &'static str {
    match s { ModState::Active => "[On] ", ModState::Faulted => "[!!] ", ModState::Disabled => "[Off]", _ => "[Off]" }
}
pub fn state_text(s: ModState) -> &'static str {
    match s {
        ModState::Active => "Loaded", ModState::Unloaded => "Not loaded", ModState::Faulted => "Crashed and disabled",
        ModState::Disabled => "Disabled in its manifest", ModState::Discovered => "Found, not loaded yet",
    }
}

impl Model {
    pub fn mods_with_settings(&self) -> impl Iterator<Item = &ModInfo> { self.mods.iter().filter(|m| m.has_settings) }

    pub fn selected(&self) -> Option<&ModInfo> {
        let id = self.selected_mod.as_deref()?;
        self.mods.iter().find(|m| m.id == id)
    }

    /// New mod list from the host. Keeps the selection if the mod still exists.
    pub fn set_mods(&mut self, mods: Vec<ModInfo>) {
        self.mods = mods;
        self.finish_pending_op();
        if self.selected().is_none() { self.selected_mod = self.mods.first().map(|m| m.id.clone()); }
        if !self.mods_with_settings().any(|m| Some(m.id.as_str()) == self.settings_mod.as_deref()) {
            let next = self.mods_with_settings().next().map(|m| m.id.clone());
            self.settings_mod = next;
            self.entries.clear();
        }
    }

    /// Clear the "Loading..." / "Unloading..." message once the host has done it.
    fn finish_pending_op(&mut self) {
        let Some((id, op, polls)) = self.pending_op.clone() else { return };
        let Some(m) = self.mods.iter().find(|m| m.id == id).cloned() else { self.pending_op = None; return };
        let (done, failed) = match op {
            ModOp::Load => (m.state == ModState::Active, m.state == ModState::Faulted),
            ModOp::Unload => (m.state != ModState::Active, false),
            ModOp::Reload => (m.state == ModState::Active && polls >= 2, m.state == ModState::Faulted),
        };
        if failed { self.status = format!("{} crashed and was disabled.", m.name); self.pending_op = None; }
        else if done { self.status.clear(); self.pending_op = None; }
        else { self.pending_op = Some((id, op, polls.saturating_add(1))); }
    }

    /// Mods that name `id` in their `after` ordering list and are loaded.
    pub fn dependents_of(&self, id: &str) -> Vec<String> {
        self.mods.iter()
            .filter(|m| m.state == ModState::Active && m.id != id && m.after.split(',').any(|a| a.trim() == id))
            .map(|m| m.name.clone()).collect()
    }

    /// Whether `def` is shown given the other entries' current values.
    pub fn visible(&self, def: &Def) -> bool {
        let Some((key, want)) = &def.depends else { return true };
        match self.entries.iter().find(|e| &e.def.key == key).map(|e| &e.value) {
            Some(Value::Bool(b)) => *b as i32 == *want,
            Some(Value::Enum(i)) => *i as i32 == *want,
            Some(Value::Int(i)) => *i == *want as i64,
            Some(Value::Keybind(k)) => *k == *want,
            _ => true,
        }
    }

    fn toggle_command(&mut self) -> Vec<Command> {
        let Some(m) = self.selected().cloned() else { return vec![] };
        if m.is_self { self.status = "The Mod Manager is always loaded.".into(); return vec![]; }
        if !m.has_code { self.status = format!("{} has no code to load.", m.name); return vec![]; }
        match m.state {
            ModState::Active => {
                let deps = self.dependents_of(&m.id);
                self.status = if deps.is_empty() { format!("Unloading {}...", m.name) }
                    else { format!("Unloading {}. {} load after it (ordering only) and keep running.", m.name, deps.join(", ")) };
                self.pending_op = Some((m.id.clone(), ModOp::Unload, 0));
                vec![Command::Request { id: m.id, op: ModOp::Unload }]
            }
            ModState::Disabled => { self.status = format!("{} is disabled in its manifest.", m.name); vec![] }
            _ => {
                self.status = format!("Loading {}...", m.name);
                self.pending_op = Some((m.id.clone(), ModOp::Load, 0));
                vec![Command::Request { id: m.id, op: ModOp::Load }]
            }
        }
    }

    fn write(&mut self, key: &str, value: Value) -> Vec<Command> {
        let Some(id) = self.settings_mod.clone() else { return vec![] };
        if let Some(e) = self.entries.iter_mut().find(|e| e.def.key == key) { e.value = value.clone(); }
        vec![Command::Write { id, key: key.to_owned(), value }]
    }

    /// Handle a widget event from the screen (`key` = source widget).
    pub fn on_event(&mut self, key: &str, kind: EventKind) -> Vec<Command> {
        match (key, kind) {
            (_, EventKind::TabChanged(i)) => {
                self.tab = i;
                self.in_controls = false;
                self.in_detail = false;
                if i == TAB_SETTINGS { return self.settings_mod.clone().map(Command::LoadSettings).into_iter().collect(); }
                vec![]
            }
            ("mods-list", EventKind::Selected(id)) => { self.selected_mod = Some(id); self.status.clear(); vec![] }
            // X on a mod opens its actions on the right; the buttons there do the work.
            ("mods-list", EventKind::RowActivated(id)) => { self.selected_mod = Some(id); self.in_detail = true; vec![Command::FocusDetail] }
            ("toggle", EventKind::Activated) => self.toggle_command(),
            ("uninstall", EventKind::Activated) => match self.selected().cloned() {
                Some(m) if m.is_self => { self.status = "The mod manager cannot uninstall itself.".into(); vec![] }
                Some(m) => { self.status = format!("Uninstalling {}...", m.name); self.in_detail = false; vec![Command::Uninstall(m.id), Command::FocusMods] }
                None => vec![],
            },
            ("reload", EventKind::Activated) => match self.selected().cloned() {
                Some(m) if m.state == ModState::Active && !m.is_self && m.has_code => {
                    self.status = format!("Reloading {}...", m.name);
                    self.pending_op = Some((m.id.clone(), ModOp::Reload, 0));
                    vec![Command::Request { id: m.id, op: ModOp::Reload }]
                }
                _ => { self.status = "Only a loaded mod can be reloaded.".into(); vec![] }
            },
            ("settings-list", EventKind::Selected(id)) => {
                self.settings_mod = Some(id.clone());
                self.entries.clear();
                vec![Command::LoadSettings(id)]
            }
            ("settings-list", EventKind::RowActivated(_)) => {
                if self.entries.is_empty() { self.status = "This mod has no settings yet.".into(); vec![] }
                else { self.in_controls = true; vec![Command::FocusControls] }
            }
            ("reset", EventKind::Activated) => {
                let Some(id) = self.settings_mod.clone() else { return vec![] };
                let writes: Vec<Command> = self.entries.iter().map(|e| e.def.default_value()).zip(self.entries.clone())
                    .map(|(v, e)| Command::Write { id: id.clone(), key: e.def.key, value: v }).collect();
                for e in &mut self.entries { e.value = e.def.default_value(); }
                self.status = "Restored defaults.".into();
                writes
            }
            (k, kind) if k.starts_with(CONTROL) => self.control_event(&k[CONTROL.len()..], kind),
            (_, EventKind::Back) => {
                if self.tab == TAB_SETTINGS && self.in_controls { self.in_controls = false; vec![Command::FocusList] }
                else if self.tab == TAB_MODS && self.in_detail { self.in_detail = false; vec![Command::FocusMods] }
                else { vec![Command::Close] }
            }
            _ => vec![],
        }
    }

    fn control_event(&mut self, key: &str, kind: EventKind) -> Vec<Command> {
        let Some(e) = self.entries.iter().find(|e| e.def.key == key).cloned() else { return vec![] };
        match (&e.def.kind, kind) {
            (Kind::Bool { .. }, EventKind::Toggled(b)) => self.write(key, Value::Bool(b)),
            (Kind::Int { .. }, EventKind::ValueChanged(v)) => self.write(key, Value::Int(v.round() as i64)),
            (Kind::Float { .. }, EventKind::ValueChanged(v)) => self.write(key, Value::Float(v as f64)),
            (Kind::Enum { .. }, EventKind::ChoiceChanged(i)) => self.write(key, Value::Enum(i)),
            (Kind::Keybind { .. }, EventKind::ChoiceChanged(i)) => {
                let code = key_options(&e.value).get(i).map(|(k, _)| *k);
                match code { Some(k) => self.write(key, Value::Keybind(k)), None => vec![] }
            }
            (Kind::Text { max_len, .. }, EventKind::Activated) => {
                let initial = e.value.as_text().unwrap_or("").to_owned();
                vec![Command::EditText { id: self.settings_mod.clone().unwrap_or_default(), key: key.to_owned(), initial, max_bytes: *max_len }]
            }
            _ => vec![],
        }
    }
}

/// Choices offered for a keybind setting; the current key is included even if unlisted.
pub fn key_options(current: &Value) -> Vec<(i32, String)> {
    let mut v: Vec<(i32, String)> = bbmod::key::PICKER.iter().map(|(k, n)| (*k, (*n).to_string())).collect();
    if let Value::Keybind(k) = current {
        if !v.iter().any(|(c, _)| c == k) { v.push((*k, bbmod::key::name(*k))); }
    }
    v
}

#[cfg(test)]
pub fn info(id: &str, name: &str, state: ModState, settings: bool) -> ModInfo {
    ModInfo { id: id.into(), name: name.into(), version: (1, 2, 3), author: "A".into(), description: "D".into(), last_error: String::new(),
        after: String::new(), conflicts: String::new(), state, has_code: true, hot_reload: true, is_self: false, has_settings: settings }
}

#[cfg(test)]
mod tests {
    use super::*;
    use bbmod::settings::Def;

    fn model() -> Model {
        let mut m = Model::default();
        let mut menu = info("bbmod.manager", "Mod Manager", ModState::Active, true);
        menu.is_self = true;
        let mut late = info("c.late", "Late", ModState::Active, false);
        late.after = "a.core, other".into();
        m.set_mods(vec![menu, info("a.core", "Core", ModState::Active, true), late, info("d.off", "Off", ModState::Unloaded, false)]);
        m
    }
    fn entry(def: Def) -> Entry { let value = def.default_value(); Entry { def, value, inactive: false } }

    #[test]
    fn toggling_requests_the_right_operation_and_protects_the_menu() {
        let mut m = model();
        m.selected_mod = Some("bbmod.manager".into());
        assert!(m.on_event("toggle", EventKind::Activated).is_empty() && m.status.contains("always loaded"));
        m.selected_mod = Some("a.core".into());
        assert_eq!(m.on_event("toggle", EventKind::Activated), [Command::Request { id: "a.core".into(), op: ModOp::Unload }]);
        assert!(m.status.contains("Late"), "{}", m.status); // warns about the mod ordered after it
        m.selected_mod = Some("d.off".into());
        assert_eq!(m.on_event("mods-list", EventKind::RowActivated("d.off".into())), [Command::FocusDetail]); // X opens the actions
        assert!(m.in_detail);
        assert_eq!(m.on_event("toggle", EventKind::Activated), [Command::Request { id: "d.off".into(), op: ModOp::Load }]);
        assert_eq!(m.on_event("root", EventKind::Back), [Command::FocusMods]);
        m.selected_mod = Some("a.core".into());
        assert_eq!(m.on_event("uninstall", EventKind::Activated), [Command::Uninstall("a.core".into()), Command::FocusMods]);
        m.selected_mod = Some("bbmod.manager".into());
        assert!(m.on_event("uninstall", EventKind::Activated).is_empty() && m.status.contains("cannot uninstall"));
        m.selected_mod = Some("d.off".into());
        assert!(m.on_event("reload", EventKind::Activated).is_empty()); // not loaded
        m.selected_mod = Some("a.core".into());
        assert_eq!(m.on_event("reload", EventKind::Activated), [Command::Request { id: "a.core".into(), op: ModOp::Reload }]);
    }

    #[test]
    fn busy_messages_clear_when_the_host_has_finished() {
        let mut m = model();
        m.selected_mod = Some("a.core".into());
        m.on_event("toggle", EventKind::Activated); // Unloading...
        assert!(m.status.starts_with("Unloading"));
        let mut still = m.mods.clone();
        m.set_mods(still.clone());
        assert!(m.status.starts_with("Unloading"), "{}", m.status); // not done yet
        still[1].state = ModState::Unloaded;
        m.set_mods(still.clone());
        assert!(m.status.is_empty(), "{}", m.status);
        m.on_event("toggle", EventKind::Activated); // Loading...
        assert!(m.status.starts_with("Loading"));
        still[1].state = ModState::Faulted;
        m.set_mods(still);
        assert!(m.status.contains("crashed"), "{}", m.status);
    }

    #[test]
    fn selection_survives_snapshots_and_falls_back_when_the_mod_disappears() {
        let mut m = model();
        m.on_event("mods-list", EventKind::Selected("c.late".into()));
        let mut next = m.mods.clone(); next[2].state = ModState::Unloaded;
        m.set_mods(next);
        assert_eq!(m.selected_mod.as_deref(), Some("c.late"));
        m.set_mods(m.mods[..2].to_vec());
        assert_eq!(m.selected_mod.as_deref(), Some("bbmod.manager"));
        assert_eq!(m.settings_mod.as_deref(), Some("bbmod.manager"));
    }

    #[test]
    fn settings_flow_loads_edits_validates_and_backs_out() {
        let mut m = model();
        assert_eq!(m.on_event("tabs", EventKind::TabChanged(TAB_SETTINGS)), [Command::LoadSettings("bbmod.manager".into())]);
        assert_eq!(m.on_event("settings-list", EventKind::Selected("a.core".into())), [Command::LoadSettings("a.core".into())]);
        assert_eq!(m.on_event("settings-list", EventKind::RowActivated("a.core".into())).len(), 0); // nothing loaded yet
        m.entries = vec![
            entry(Def::bool("on", "On", false)), entry(Def::int("lvl", "Level", 0, 10, 3).depends_on("on", 1)),
            entry(Def::choice("mode", "Mode", &["A", "B"], 0)), entry(Def::text("name", "Name", 8, "x")),
            entry(Def::keybind("key", "Key", bbmod::key::F4)), entry(Def::float("f", "F", 0.0, 1.0, 0.5)),
        ];
        assert!(!m.visible(&m.entries[1].def.clone()));
        assert_eq!(m.on_event("settings-list", EventKind::RowActivated("a.core".into())), [Command::FocusControls]);
        let w = |k: &str, v: Value| Command::Write { id: "a.core".into(), key: k.into(), value: v };
        assert_eq!(m.on_event("s:on", EventKind::Toggled(true)), [w("on", Value::Bool(true))]);
        assert!(m.visible(&m.entries[1].def.clone())); // dependency now met
        assert_eq!(m.on_event("s:lvl", EventKind::ValueChanged(6.6)), [w("lvl", Value::Int(7))]);
        assert_eq!(m.on_event("s:mode", EventKind::ChoiceChanged(1)), [w("mode", Value::Enum(1))]);
        assert_eq!(m.on_event("s:f", EventKind::ValueChanged(0.75)), [w("f", Value::Float(0.75))]);
        let idx = key_options(&Value::Keybind(bbmod::key::F4)).iter().position(|(k, _)| *k == bbmod::key::F1).unwrap();
        assert_eq!(m.on_event("s:key", EventKind::ChoiceChanged(idx)), [w("key", Value::Keybind(bbmod::key::F1))]);
        assert_eq!(m.on_event("s:name", EventKind::Activated),
            [Command::EditText { id: "a.core".into(), key: "name".into(), initial: "x".into(), max_bytes: 8 }]);
        assert!(m.on_event("s:missing", EventKind::Toggled(true)).is_empty());
        let reset = m.on_event("reset", EventKind::Activated);
        assert_eq!(reset.len(), 6);
        assert_eq!(m.entries[0].value, Value::Bool(false));
        assert_eq!(m.on_event("root", EventKind::Back), [Command::FocusList]);
        assert_eq!(m.on_event("root", EventKind::Back), [Command::Close]);
    }

    #[test]
    fn unlisted_keys_stay_selectable() {
        let opts = key_options(&Value::Keybind(200));
        assert_eq!(opts.last(), Some(&(200, "Key 200".to_string())));
        assert_eq!(key_options(&Value::Keybind(bbmod::key::F4)).len(), bbmod::key::PICKER.len());
    }
}
