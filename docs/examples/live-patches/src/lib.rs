//! Toggle gameplay patches from bbport's Bloodborne.xml while the game runs.
//!
//! The entries are the real ones bbport ships (addresses for 1.09). Each is an
//! owned patch group: turning it off, unloading the mod or hot-reloading it
//! restores the original bytes, and the on/off choices survive a hot reload.
//!
//! Patches enabled in bbport's own settings are already in the game at launch;
//! toggling those here changes nothing. Use this for the ones you left off.
use bbmod::{key, Cx, Id, Mod, Result, Version};
use std::cell::RefCell;
use std::rc::Rc;

/// (XML Metadata Name, hotkey with Ctrl, label).
const ENTRIES: &[(&str, i32, &str)] = &[
    ("Player No Dead (Read note)", key::F1, "Can't die"),
    ("Player Stealth (Read note)", key::F2, "Enemies ignore you"),
    ("Player Silent (Read note)", key::F3, "Silent"),
    ("No Rally Decay", key::F4, "Rally never decays"),
    ("Disable Rally (HP Regain)", key::F5, "No rally"),
    ("Increased camera distance", key::F6, "Camera further out"),
    ("Disable Camera Auto Rotation via Movement", key::F7, "No camera auto-turn"),
    ("DS1-like physics", key::F9, "DS1 ragdolls"),
];

#[derive(Default)]
struct Entry {
    id: Option<Id>,
    error: Option<String>,
}

struct Patches {
    xml: String,
    entries: Vec<Entry>,
}

impl Patches {
    fn is_on(&self, i: usize) -> bool { self.entries[i].id.is_some() }

    fn set(&mut self, cx: &Cx, i: usize, on: bool) {
        if on == self.is_on(i) { return; }
        let e = &mut self.entries[i];
        if on {
            match cx.patch_xml(&self.xml, Some(ENTRIES[i].0)) {
                Ok(id) => { e.id = Some(id); e.error = None; }
                Err(err) => e.error = Some(format!("{err}: {}", cx.last_error())),
            }
        } else if let Some(id) = e.id.take() {
            let _ = cx.release(id);
        }
    }
}

struct LivePatches {
    patches: Rc<RefCell<Patches>>,
}

impl Mod for LivePatches {
    fn load(cx: &mut Cx) -> Result<Self> {
        let xml = cx.config("patches.xml").unwrap_or_else(|| "Bloodborne.xml".into());
        let entries = ENTRIES.iter().map(|_| Entry::default()).collect();
        let patches = Rc::new(RefCell::new(Patches { xml, entries }));

        let p = patches.clone();
        cx.on_frame(move |cx| {
            if !(cx.key_down(key::LCTRL) || cx.key_down(key::RCTRL)) { return; }
            for (i, (_, k, label)) in ENTRIES.iter().enumerate() {
                if cx.key_pressed(*k) {
                    let mut p = p.borrow_mut();
                    let on = !p.is_on(i);
                    p.set(cx, i, on);
                    cx.notify(&format!("{label}: {}", if p.is_on(i) { "on" } else { "off" }));
                }
            }
        })?;

        let p = patches.clone();
        cx.panel("Live Patches", move |ui| {
            let mut p = p.borrow_mut();
            ui.text("Ctrl + F1..F9 toggles. Off restores the original bytes.");
            ui.separator();
            for i in 0..ENTRIES.len() {
                let (name, _, label) = ENTRIES[i];
                let mut on = p.is_on(i);
                if ui.checkbox(label, &mut on) { p.set(ui.cx(), i, on); }
                if let Some(err) = &p.entries[i].error {
                    ui.text_colored([1.0, 0.45, 0.4, 1.0], &format!("  {name}: {err}"));
                }
            }
        })?;
        Ok(LivePatches { patches })
    }

    /// One byte per entry: on or off.
    fn save_state(&self) -> Option<Vec<u8>> {
        let p = self.patches.borrow();
        Some((0..ENTRIES.len()).map(|i| p.is_on(i) as u8).collect())
    }

    /// The old instance's patches were reverted; turn the same ones back on.
    fn restore_state(&mut self, cx: &mut Cx, s: &[u8], _from: Version) {
        let mut p = self.patches.borrow_mut();
        for (i, &on) in s.iter().enumerate().take(ENTRIES.len()) {
            if on != 0 { p.set(cx, i, true); }
        }
    }
}

bbmod::export!(LivePatches);
