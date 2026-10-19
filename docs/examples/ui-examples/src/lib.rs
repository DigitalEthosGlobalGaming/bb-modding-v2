use bbmod::{Cx, Id, Mod, Result, Version};
use std::{cell::RefCell, rc::Rc};

#[derive(Clone, Debug, PartialEq)]
struct Values {
    dashboard_count: u64,
    settings_count: u64,
    enabled: bool,
    level: i32,
    style: usize,
    label: String,
    settings_visible: bool,
}
impl Default for Values {
    fn default() -> Self {
        Self { dashboard_count: 0, settings_count: 0, enabled: true, level: 50,
            style: 0, label: "Shared UI example".into(), settings_visible: true }
    }
}
impl Values {
    fn encode(&self) -> Vec<u8> {
        let mut bytes = b"UIEX\x01".to_vec();
        bytes.extend(self.dashboard_count.to_le_bytes());
        bytes.extend(self.settings_count.to_le_bytes());
        bytes.extend(self.level.to_le_bytes());
        bytes.extend([self.enabled as u8, self.style as u8, self.settings_visible as u8]);
        bytes.extend(self.label.as_bytes());
        bytes
    }
    fn decode(bytes: &[u8]) -> Option<Self> {
        if bytes.len() < 28 || bytes.len() > 155 || &bytes[..5] != b"UIEX\x01" { return None; }
        let level = i32::from_le_bytes(bytes[21..25].try_into().ok()?);
        if !(0..=100).contains(&level) || bytes[25] > 1 || bytes[26] > 2 || bytes[27] > 1 { return None; }
        Some(Self {
            dashboard_count: u64::from_le_bytes(bytes[5..13].try_into().ok()?),
            settings_count: u64::from_le_bytes(bytes[13..21].try_into().ok()?),
            level, enabled: bytes[25] != 0, style: bytes[26] as usize,
            settings_visible: bytes[27] != 0,
            label: std::str::from_utf8(&bytes[28..]).ok()?.to_owned(),
        })
    }
}
#[derive(Default)]
struct State {
    values: Values,
    settings_id: Option<Id>,
    reconcile: bool,
    notice: String,
}
fn add_settings(cx: &mut Cx, state: Rc<RefCell<State>>) -> Result<Id> {
    cx.contribute("bbmod.settings", "example-controls", 100, move |ui| {
        let mut s = state.borrow_mut();
        ui.text("Shared UI Examples — controls");
        ui.text("These controls change this example's state only.");
        ui.checkbox("Enabled", &mut s.values.enabled);
        ui.slider_i32("Level", &mut s.values.level, 0, 100);
        ui.combo("Style", &mut s.values.style, &["Neutral", "Green", "Orange"]);
        ui.input_text("Label", &mut s.values.label, 128);
        // Deliberately the same label as the dashboard button.
        if ui.button("Increment") { s.values.settings_count = s.values.settings_count.saturating_add(1); }
        ui.text(&format!("Settings counter: {}", s.values.settings_count));
    })
}
struct UiExamples { state: Rc<RefCell<State>> }
impl Mod for UiExamples {
    fn load(cx: &mut Cx) -> Result<Self> {
        let state = Rc::new(RefCell::new(State::default()));
        let dashboard = state.clone();
        cx.contribute("bbmod.dashboard", "example-overview", 0, move |ui| {
            let mut s = dashboard.borrow_mut();
            ui.text("Shared UI Examples — dashboard");
            let color = match s.values.style { 1 => [0.3, 1.0, 0.5, 1.0], 2 => [1.0, 0.6, 0.2, 1.0], _ => [0.9, 0.9, 0.9, 1.0] };
            ui.text_colored(color, &s.values.label);
            if ui.button("Increment") { s.values.dashboard_count = s.values.dashboard_count.saturating_add(1); }
            ui.text(&format!("Dashboard counter: {}", s.values.dashboard_count));
            ui.text(&format!("Enabled: {} | level {}", s.values.enabled, s.values.level));
            if ui.checkbox("Show example settings contribution", &mut s.values.settings_visible) { s.reconcile = true; }
            if ui.button("Reset example counters") { s.values.dashboard_count = 0; s.values.settings_count = 0; }
            ui.text("Hide/show removes only this mod's settings contribution.");
            ui.text("Reload preserves these values; unload removes both contributions.");
            ui.text(&s.notice);
        })?;
        let id = add_settings(cx, state.clone())?;
        state.borrow_mut().settings_id = Some(id);
        let frame = state.clone();
        cx.on_frame_mut(move |cx| {
            let (reconcile, visible, id) = {
                let mut s = frame.borrow_mut();
                let needed = s.reconcile;
                s.reconcile = false;
                (needed, s.values.settings_visible, s.settings_id)
            };
            if !reconcile { return; }
            if visible && id.is_none() {
                match add_settings(cx, frame.clone()) {
                    Ok(id) => { let mut s = frame.borrow_mut(); s.settings_id = Some(id); s.notice = "Settings contribution registered".into(); }
                    Err(error) => { let mut s = frame.borrow_mut(); s.values.settings_visible = false; s.notice = format!("Cannot register settings: {error}"); }
                }
            } else if !visible {
                if let Some(id) = id {
                    match cx.release(id) {
                        Ok(()) => { let mut s = frame.borrow_mut(); s.settings_id = None; s.notice = "Only this mod's settings contribution was removed".into(); }
                        Err(error) => { let mut s = frame.borrow_mut(); s.values.settings_visible = true; s.notice = format!("Cannot remove settings: {error}"); }
                    }
                }
            }
        })?;
        cx.log_info("UI example registered bbmod.dashboard and bbmod.settings; no game writes");
        Ok(Self { state })
    }
    fn save_state(&self) -> Option<Vec<u8>> { Some(self.state.borrow().values.encode()) }
    fn restore_state(&mut self, cx: &mut Cx, bytes: &[u8], _from: Version) {
        if let Some(values) = Values::decode(bytes) {
            let mut state = self.state.borrow_mut(); state.values = values; state.reconcile = true;
        } else { cx.log_warn("UI example ignored incompatible or corrupt saved state"); }
    }
}
bbmod::export!(UiExamples);

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn restores_all_user_state_without_resource_handles() {
        let values = Values { dashboard_count: u64::MAX, settings_count: 7, enabled: false,
            level: 100, style: 2, label: "Unicode ✓".into(), settings_visible: false };
        assert_eq!(Values::decode(&values.encode()), Some(values));
    }
    #[test]
    fn rejects_old_short_invalid_and_oversized_state() {
        let valid = Values::default().encode();
        for n in 0..28 { assert_eq!(Values::decode(&valid[..n]), None); }
        for (offset, value) in [(4, 2), (25, 2), (26, 3), (27, 2), (28, 255)] {
            let mut corrupt = valid.clone(); corrupt[offset] = value;
            assert_eq!(Values::decode(&corrupt), None);
        }
        let mut oversized = valid; oversized.resize(156, b'a');
        assert_eq!(Values::decode(&oversized), None);
    }
}
