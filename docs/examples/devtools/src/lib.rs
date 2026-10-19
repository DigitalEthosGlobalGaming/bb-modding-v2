//! In-game tools for finding things to mod: address conversion, a memory peek,
//! pattern scans and a live watch list. Open bbport's menu (Insert / L3+R3).
//!
//! Addresses are typed the way you find them elsewhere: PS4 addresses (eboot at
//! 0x400000, as in XML patches, Ghidra or IDA) or host addresses ("h" prefix).
use bbmod::{Cx, Mod, Result, Ui, Version};
use std::cell::RefCell;
use std::rc::Rc;

const TYPES: &[&str] = &["u8", "i32", "u32", "f32", "u64 / pointer"];

struct Watch {
    label: String,
    addr: usize,
    ty: usize,
}

#[derive(Default)]
struct Tools {
    addr_text: String,
    peek_text: String,
    peek: Option<(usize, Vec<u8>)>,
    pattern: String,
    scan_result: String,
    watch_text: String,
    watch_label: String,
    watch_ty: usize,
    watches: Vec<Watch>,
    message: String,
}

/// "0x04d99138" = PS4 address; "h0x7ff6..." = host address.
fn parse_addr(cx: &Cx, s: &str) -> Option<usize> {
    let s = s.trim();
    let (host, s) = match s.strip_prefix('h').or_else(|| s.strip_prefix('H')) {
        Some(rest) => (true, rest),
        None => (false, s),
    };
    let digits = s.trim_start_matches("0x").trim_start_matches("0X");
    let v = u64::from_str_radix(digits, 16).ok()?;
    Some(if host { v as usize } else { cx.ps4(v) })
}

fn to_ps4(cx: &Cx, host: usize) -> u64 { (host - cx.eboot_base()) as u64 + 0x400000 }

fn show(cx: &Cx, addr: usize, ty: usize) -> String {
    let r = match ty {
        0 => cx.read_val::<u8>(addr).map(|v| format!("{v} (0x{v:02x})")),
        1 => cx.read_val::<i32>(addr).map(|v| format!("{v}")),
        2 => cx.read_val::<u32>(addr).map(|v| format!("{v} (0x{v:08x})")),
        3 => cx.read_val::<f32>(addr).map(|v| format!("{v:.4}")),
        _ => cx.read_val::<u64>(addr).map(|v| format!("0x{v:016x}")),
    };
    r.unwrap_or_else(|_| "unreadable".into())
}

impl Tools {
    fn draw(&mut self, ui: &Ui) {
        let cx = ui.cx();
        ui.text(&format!("eboot at host 0x{:x} (PS4 0x400000)", cx.eboot_base()));
        let s = cx.session();
        if s.in_session != 0 {
            ui.text(&format!("online: {} peer(s), owner {}", s.peer_count, if s.is_owner != 0 { "me" } else { "other" }));
        }

        if ui.collapsing_header("Address converter") {
            ui.input_text("address##conv", &mut self.addr_text, 32);
            if let Some(a) = parse_addr(cx, &self.addr_text) {
                ui.text(&format!("host 0x{a:x}   PS4 0x{:x}", to_ps4(cx, a)));
            }
        }

        if ui.collapsing_header("Memory peek") {
            ui.input_text("address##peek", &mut self.peek_text, 32);
            ui.same_line();
            if ui.button("Read 64 bytes") {
                self.peek = parse_addr(cx, &self.peek_text).map(|a| {
                    let mut buf = vec![0u8; 64];
                    match cx.read(a, &mut buf) { Ok(()) => (a, buf), Err(_) => (a, Vec::new()) }
                });
            }
            if let Some((a, bytes)) = &self.peek {
                if bytes.is_empty() {
                    ui.text_colored([1.0, 0.5, 0.4, 1.0], "unreadable (not mapped)");
                }
                for (row, chunk) in bytes.chunks(16).enumerate() {
                    let hex: Vec<String> = chunk.iter().map(|b| format!("{b:02x}")).collect();
                    ui.text(&format!("{:08x}  {}", to_ps4(cx, a + row * 16), hex.join(" ")));
                }
            }
        }

        if ui.collapsing_header("Pattern scan") {
            ui.text("IDA style, ?? for any byte. Scans the whole eboot.");
            ui.input_text("pattern", &mut self.pattern, 128);
            if ui.button("Scan") {
                self.scan_result = match cx.scan(&self.pattern, 0, 0) {
                    Ok(a) => format!("found at PS4 0x{:x} (host 0x{a:x})", to_ps4(cx, a)),
                    Err(e) => format!("not found ({e})"),
                };
            }
            if !self.scan_result.is_empty() { ui.text(&self.scan_result); }
        }

        if ui.collapsing_header("Watch list") {
            ui.input_text("address##watch", &mut self.watch_text, 32);
            ui.input_text("label", &mut self.watch_label, 32);
            ui.combo("type", &mut self.watch_ty, TYPES);
            if ui.button("Add") {
                match parse_addr(cx, &self.watch_text) {
                    Some(addr) => {
                        let label = if self.watch_label.is_empty() { self.watch_text.clone() } else { self.watch_label.clone() };
                        self.watches.push(Watch { label, addr, ty: self.watch_ty });
                        self.message.clear();
                    }
                    None => self.message = "not an address".into(),
                }
            }
            if !self.message.is_empty() { ui.text(&self.message); }
            let mut remove = None;
            for (i, w) in self.watches.iter().enumerate() {
                ui.text(&format!("{} [{}] = {}", w.label, TYPES[w.ty], show(cx, w.addr, w.ty)));
                ui.same_line();
                if ui.button(&format!("x##{i}")) { remove = Some(i); }
            }
            if let Some(i) = remove { self.watches.remove(i); }
        }
    }
}

struct DevTools {
    tools: Rc<RefCell<Tools>>,
}

impl Mod for DevTools {
    fn load(cx: &mut Cx) -> Result<Self> {
        let tools = Rc::new(RefCell::new(Tools::default()));
        let t = tools.clone();
        cx.panel("Dev Tools", move |ui| t.borrow_mut().draw(ui))?;
        Ok(DevTools { tools })
    }

    /// Keep the watch list across hot reloads: "label\tPS4 addr\ttype" lines.
    fn save_state(&self) -> Option<Vec<u8>> {
        let t = self.tools.borrow();
        let lines: Vec<String> = t.watches.iter().map(|w| format!("{}\t{}\t{}", w.label, w.addr, w.ty)).collect();
        Some(lines.join("\n").into_bytes())
    }

    fn restore_state(&mut self, _cx: &mut Cx, s: &[u8], _from: Version) {
        let mut t = self.tools.borrow_mut();
        for line in String::from_utf8_lossy(s).lines() {
            let f: Vec<&str> = line.split('\t').collect();
            if let [label, addr, ty] = f[..] {
                if let (Ok(addr), Ok(ty)) = (addr.parse(), ty.parse::<usize>()) {
                    t.watches.push(Watch { label: label.into(), addr, ty: ty.min(TYPES.len() - 1) });
                }
            }
        }
    }
}

bbmod::export!(DevTools);
