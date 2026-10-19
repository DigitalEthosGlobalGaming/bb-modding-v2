//! Smallest useful mod: a panel with a frame counter that survives hot reload.
use bbmod::{Cx, Mod, Result, Version};
use std::cell::Cell;
use std::rc::Rc;

struct Hello {
    frames: Rc<Cell<u64>>,
    reloads: u32,
}

impl Mod for Hello {
    fn load(cx: &mut Cx) -> Result<Self> {
        let frames = Rc::new(Cell::new(0u64));

        let f = frames.clone();
        cx.on_frame(move |_cx| f.set(f.get() + 1))?;

        let f = frames.clone();
        cx.panel("Hello (Rust)", move |ui| {
            ui.text(&format!("frames seen: {}", f.get()));
        })?;

        cx.log_info("hello from Rust");
        Ok(Hello { frames, reloads: 0 })
    }

    fn save_state(&self) -> Option<Vec<u8>> {
        let mut v = self.frames.get().to_le_bytes().to_vec();
        v.extend_from_slice(&(self.reloads + 1).to_le_bytes());
        Some(v)
    }

    fn restore_state(&mut self, _cx: &mut Cx, s: &[u8], _from: Version) {
        if s.len() == 12 {
            self.frames.set(u64::from_le_bytes(s[..8].try_into().unwrap()));
            self.reloads = u32::from_le_bytes(s[8..12].try_into().unwrap());
        }
    }
}

bbmod::export!(Hello);
