use bbmod::{Cx, Event, Mod, PadChange, Result, Version};
use std::{cell::Cell, rc::Rc};

struct Counter { presses: Rc<Cell<u64>> }
impl Mod for Counter {
    fn load(cx: &mut Cx) -> Result<Self> {
        cx.subscribe(bbmod::sys::BBM_EV_PAD_CHANGED)?;
        let presses = Rc::new(Cell::new(0));
        let count = presses.clone();
        cx.panel("Shared Input Counter", move |ui| {
            ui.text(&format!("Button presses observed: {}", count.get()));
            ui.text("Count survives Reload. No game memory is changed.");
        })?;
        Ok(Self { presses })
    }
    fn event(&mut self, cx: &mut Cx, event: &Event) {
        if let Some(change) = PadChange::from_event(event) {
            self.presses.set(self.presses.get().saturating_add(change.pressed().count_ones() as u64));
            bbmod::info!(cx, "input: total presses={}", self.presses.get());
        }
    }
    fn save_state(&self) -> Option<Vec<u8>> { Some(self.presses.get().to_le_bytes().to_vec()) }
    fn restore_state(&mut self, _cx: &mut Cx, bytes: &[u8], _from: Version) {
        if let Ok(value) = <[u8; 8]>::try_from(bytes) { self.presses.set(u64::from_le_bytes(value)); }
    }
}
bbmod::export!(Counter);
