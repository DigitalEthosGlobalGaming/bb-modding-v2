use bbmod::{Cx, Event, Mod, PadChange, Result};
use std::{cell::Cell, rc::Rc};

struct Monitor { buttons: Rc<Cell<u32>> }
impl Mod for Monitor {
    fn load(cx: &mut Cx) -> Result<Self> {
        cx.subscribe(bbmod::sys::BBM_EV_PAD_CHANGED)?;
        let buttons = Rc::new(Cell::new(0));
        let state = buttons.clone();
        cx.panel("Shared Input Monitor", move |ui| {
            ui.text(&format!("Observed buttons: 0x{:08x}", state.get()));
            ui.text("Close F1 to send controller input to the game. See bbmods.log.");
        })?;
        Ok(Self { buttons })
    }
    fn event(&mut self, cx: &mut Cx, event: &Event) {
        if let Some(change) = PadChange::from_event(event) {
            self.buttons.set(change.current);
            bbmod::info!(cx, "input: current=0x{:08x} pressed=0x{:08x} released=0x{:08x}",
                change.current, change.pressed(), change.released());
        }
    }
}
bbmod::export!(Monitor);
