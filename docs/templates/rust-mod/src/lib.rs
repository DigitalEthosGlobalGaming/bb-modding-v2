//! __NAME__: a bbport code mod. Edit, build with Cargo, deploy using the toolkit, and the game
//! (started with BB_MODS_DEV=1) hot-reloads it on every save.
use bbmod::{key, Cx, Mod, Result, Version};
use std::cell::Cell;
use std::rc::Rc;

struct __TYPE__ {
    presses: Rc<Cell<u32>>,
}

impl Mod for __TYPE__ {
    fn load(cx: &mut Cx) -> Result<Self> {
        bbmod::info!(cx, "__NAME__ {} loaded", cx.config("mod.version").unwrap_or_default());
        let presses = Rc::new(Cell::new(0));

        // Every frame, on the game thread. Ctrl+F8 is this mod's hotkey.
        let p = presses.clone();
        cx.on_frame(move |cx| {
            if cx.key_down(key::LCTRL) && cx.key_pressed(key::F8) {
                p.set(p.get() + 1);
                cx.notify(&format!("hotkey pressed {} times", p.get()));
            }
        })?;

        // Shown while bbport's menu is open (Insert / L3+R3).
        let p = presses.clone();
        cx.panel("__NAME__", move |ui| {
            ui.text("Hello from __NAME__.");
            ui.text(&format!("Ctrl+F8 pressed {} times", p.get()));
        })?;

        // Next steps (see docs/MODDING.md):
        //   cx.patch_xml("Bloodborne.xml", Some("No Rally Decay"))?;  // a bbport patch, live
        //   cx.patch(cx.ps4(0x0123_4567), &[0x90, 0x90])?;            // your own bytes
        //   my_hook::install(cx, cx.ps4(0x0123_4567))?;               // bbmod::detour!
        Ok(__TYPE__ { presses })
    }

    // Survive hot reload: whatever bytes you return come back in restore_state.
    fn save_state(&self) -> Option<Vec<u8>> { Some(self.presses.get().to_le_bytes().to_vec()) }
    fn restore_state(&mut self, _cx: &mut Cx, s: &[u8], _from: Version) {
        if let Ok(b) = s.try_into() { self.presses.set(u32::from_le_bytes(b)); }
    }
}

bbmod::export!(__TYPE__);
