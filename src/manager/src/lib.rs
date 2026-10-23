//! Mod Manager: an in-game list of installed mods (turn them on and off, see their state and
//! errors) and an editor for the settings each mod declares. It is an ordinary mod with the
//! `manage` capability (`[mod] manage = true`); it cannot unload itself.
//!
//! Layout:
//! * `model`  pure state and behaviour (events in, commands out); unit tested
//! * `view`   the widget tree and its in-place sync from the model
//! * this file wires them to the host: open gestures, snapshot polling, commands
//!
//! Open it from the pause menu's "Mods" entry (which it replaces while loaded), with the key in "Open key" (default
//! F2), or with the pad combo in "Pad combo" (default L1 + R1 + Touchpad). The key and combo are
//! this mod's own settings, shown in its Settings tab.
mod browse;
mod catalog;
mod install;
mod jobs;
mod model;
mod view;

use bbmod::native_menu::{self as native, Change, Kind as NodeKind, Node as NativeNode};
use bbmod::settings::{self, Def, ModOp, Value};
use bbmod::text_entry::{State as TextState, TextEntry};
use bbmod::ui_layer::{self, EventKind, HostScreen, LayerKind};
use bbmod::{pad, Cx, Event, Mod, Result};
use browse::{Browse, Out, TAB_BROWSE};
use jobs::{Done, Job};
use model::{Command, Model, TAB_SETTINGS};
use std::sync::mpsc::{channel, Receiver, Sender};
use std::cell::RefCell;
use std::rc::Rc;
use view::View;

/// Frames between host snapshots while the menu is open.
const POLL_FRAMES: u32 = 20;
/// The pause menu's built-in "Mods" entry is replaced by one that opens this menu: the host
/// accepts this key under `game.system` only from a mod with the manage capability.
const NATIVE_OPEN: &str = "mods-root";
const PAD_COMBOS: [&str; 3] = ["Off", "L1 + R1 + Touchpad", "L2 + R2 + Touchpad"];

struct State {
    model: Model,
    view: View,
    screen: HostScreen,
    open: bool,
    since_poll: u32,
    /// Frames left of per-frame polling after a load/unload request (it runs at the sync point).
    fast_poll: u32,
    dirty: bool,
    text: Option<(String, String, TextEntry)>,
    open_key: i32,
    pad_combo: usize,
    browse: Browse,
    jobs: Option<Sender<Job>>,
    done: Option<Receiver<Done>>,
    store: Option<bbmod::store::Store>,
    browse_started: bool,
}

const OWN_ID: &str = "bbmod.manager";
const SOURCES_BLOB: &str = "sources.json";
/// Marks the text dialog that asks for a new mod list source (it is not a setting key).
const ADD_SOURCE: &str = "@add-source";

impl State {
    fn new() -> Self {
        Self { model: Model::default(), view: View::default(), screen: HostScreen::new(LayerKind::Screen, 100, view::build()),
            open: false, since_poll: 0, fast_poll: 0, dirty: true, text: None, open_key: bbmod::key::F2, pad_combo: 1,
            browse: Browse::new(Vec::new()), jobs: None, done: None, store: None, browse_started: false }
    }

    fn refresh(&mut self, cx: &Cx) {
        match cx.manager_mods() {
            Ok(mods) => self.model.set_mods(mods),
            Err(e) => self.model.status = format!("Cannot list mods: {e}"),
        }
        if self.model.tab == TAB_SETTINGS { self.load_settings(cx); }
        self.dirty = true;
    }

    fn load_settings(&mut self, cx: &Cx) {
        let Some(id) = self.model.settings_mod.clone() else { self.model.entries.clear(); return };
        match cx.manager_settings(&id) {
            Ok(e) => { if e != self.model.entries { self.model.entries = e; } }
            Err(e) => { self.model.entries.clear(); self.model.status = format!("Cannot read settings: {e}"); }
        }
        self.dirty = true;
    }

    fn set_open(&mut self, cx: &Cx, open: bool) {
        self.open = open;
        self.screen.set_visible(open);
        if open { self.model.status.clear(); self.refresh(cx); }
        self.dirty = true;
    }

    fn run(&mut self, cx: &mut Cx, commands: Vec<Command>) {
        for c in commands {
            match c {
                Command::Close => self.set_open(cx, false),
                Command::Request { id, op } => {
                    if let Err(e) = cx.manager_request(&id, op) { self.model.status = format!("Request failed: {e}"); }
                    self.fast_poll = 12; // the host applies it at the next sync point
                }
                Command::Write { id, key, value } => {
                    if let Err(e) = cx.manager_write_setting(&id, &key, &value) {
                        self.model.status = format!("Cannot change {key}: {e}");
                        self.load_settings(cx); // show what the host actually holds
                    }
                }
                Command::EditText { id, key, initial, max_bytes } => {
                    match cx.text_entry(&format!("Edit {key}"), &initial, max_bytes.clamp(1, 95)) {
                        Ok(t) => self.text = Some((id, key, t)),
                        Err(e) => self.model.status = format!("Cannot open the text dialog: {e}"),
                    }
                }
                Command::LoadSettings(_) => self.load_settings(cx),
                Command::FocusControls => {
                    let first = self.model.entries.iter().find(|e| self.model.visible(&e.def)).map(|e| format!("{}{}", model::CONTROL, e.def.key));
                    if let Some(k) = first { self.screen.update(|_| {}); self.sync_now(); self.screen.set_focus(&k); }
                }
                Command::FocusList => { self.screen.set_focus("settings-list"); }
            }
        }
        self.dirty = true;
    }

    fn sync_now(&mut self) {
        let focus = self.screen.focus().map(str::to_owned);
        let (model, view, browse) = (&self.model, &mut self.view, &self.browse);
        self.screen.update(|root| { view.sync(root, model, focus.as_deref()); view::sync_browse(root, browse, &model.mods); });
    }

    fn frame(&mut self, cx: &mut Cx) {
        if let Some((id, key, t)) = &mut self.text {
            match t.poll(cx) {
                Ok(TextState::Accepted(s)) => {
                    let (id, key) = (id.clone(), key.clone());
                    self.text = None;
                    if key == ADD_SOURCE {
                        let outs = self.browse.add_source(&s);
                        self.apply(cx, outs);
                    } else {
                        self.run(cx, vec![Command::Write { id, key, value: Value::Text(s) }]);
                        self.load_settings(cx);
                    }
                }
                Ok(TextState::Pending) => {}
                _ => { self.text = None; }
            }
        }
        self.drain_jobs(cx);
        if !self.open {
            if self.browse.active() {
                self.since_poll += 1;
                if self.fast_poll > 0 || self.since_poll >= POLL_FRAMES { self.fast_poll = self.fast_poll.saturating_sub(1); self.since_poll = 0; self.poll(cx); }
            }
            let combo = match self.pad_combo { 1 => cx.pad_pressed(pad::L1 | pad::R1 | pad::TOUCHPAD), 2 => cx.pad_pressed(pad::L2 | pad::R2 | pad::TOUCHPAD), _ => false };
            if cx.key_pressed(self.open_key) || combo { self.set_open(cx, true); }
        } else {
            self.since_poll += 1;
            if self.fast_poll > 0 || self.since_poll >= POLL_FRAMES {
                self.fast_poll = self.fast_poll.saturating_sub(1);
                self.since_poll = 0;
                self.poll(cx);
            }
        }
        if self.dirty || self.open {
            self.sync_now();
            self.dirty = false;
        }
        if self.open || self.screen.id().is_some() {
            if let Err(e) = self.screen.publish(cx) { cx.log_warn(&format!("menu: cannot publish: {e}")); }
        }
    }

    /// Load the saved mod list sources and start the download worker.
    fn start_network(&mut self, cx: &mut Cx) {
        self.store = bbmod::store::Store::open(cx).ok();
        let saved: Vec<String> = self.store.as_ref().and_then(|s| s.read_blob(SOURCES_BLOB).ok().flatten())
            .and_then(|b| serde_json::from_slice(&b).ok()).unwrap_or_default();
        self.browse = Browse::new(saved);
        let Some(mods) = std::env::var_os(bbmod::store::ENV_MODS_DIR).map(std::path::PathBuf::from) else {
            cx.log_warn("manager: no mods folder known; the Browse tab cannot install");
            return;
        };
        let dirs = install::Dirs::new(&mods, OWN_ID);
        let (jtx, jrx) = channel();
        let (dtx, drx) = channel();
        match cx.spawn(move |stop| jobs::worker(jrx, dtx, dirs, stop)) {
            Ok(_) => { self.jobs = Some(jtx); self.done = Some(drx); }
            Err(e) => cx.log_warn(&format!("manager: cannot start the download worker: {e}")),
        }
    }

    /// Refresh the mod snapshot, then let the Browse tab act on it (waits for unload/load).
    fn poll(&mut self, cx: &mut Cx) {
        self.refresh(cx);
        let outs = self.browse.tick(&self.model.mods);
        self.apply(cx, outs);
    }

    fn drain_jobs(&mut self, cx: &mut Cx) {
        let mut got = Vec::new();
        if let Some(rx) = &self.done { while let Ok(d) = rx.try_recv() { got.push(d); } }
        for d in got {
            let outs = self.browse.on_done(d, &self.model.mods);
            self.apply(cx, outs);
            self.dirty = true;
        }
    }

    fn apply(&mut self, cx: &mut Cx, outs: Vec<Out>) {
        for o in outs {
            match o {
                Out::Job(j) => {
                    let sent = self.jobs.as_ref().is_some_and(|tx| tx.send(j).is_ok());
                    if !sent { self.browse.status = "Mod downloads are unavailable (no mods folder).".into(); }
                }
                Out::Unload(id) => { if let Err(e) = cx.manager_request(&id, ModOp::Unload) { self.browse.status = format!("Cannot unload {id}: {e}"); } self.fast_poll = 12; }
                Out::Load(id) => { if let Err(e) = cx.manager_request(&id, ModOp::Load) { self.browse.status = format!("Cannot load {id}: {e}"); } self.fast_poll = 12; }
                Out::SaveSources(v) => {
                    if let (Some(store), Ok(json)) = (&self.store, serde_json::to_vec(&v)) { let _ = store.write_blob(SOURCES_BLOB, &json); }
                }
                Out::AddSourceDialog => match cx.text_entry("Add a mod list: owner/repo or an https link", "", 95) {
                    Ok(t) => self.text = Some((String::new(), ADD_SOURCE.into(), t)),
                    Err(e) => self.browse.status = format!("Cannot open the text dialog: {e}"),
                },
            }
        }
        self.dirty = true;
    }

    fn apply_own_setting(&mut self, key: &str, value: &Value) {
        match (key, value) {
            ("open_key", Value::Keybind(k)) => self.open_key = *k,
            ("pad_combo", Value::Enum(i)) => self.pad_combo = *i,
            _ => {}
        }
    }
}

struct MenuMod { state: Rc<RefCell<State>> }

impl Mod for MenuMod {
    fn load(cx: &mut Cx) -> Result<Self> {
        cx.subscribe(ui_layer::EVENTS | settings::EVENTS | native::EVENTS)?;
        cx.declare_setting(&Def::keybind("open_key", "Open key", bbmod::key::F2).group("Opening").order(0)
            .help("Keyboard key that opens this menu."))?;
        cx.declare_setting(&Def::choice("pad_combo", "Pad combo", &PAD_COMBOS, 1).group("Opening").order(1)
            .help("Controller buttons that open this menu. The game still sees the buttons you press."))?;
        let mut s = State::new();
        s.start_network(cx);
        if let Ok(Value::Keybind(k)) = cx.setting("open_key") { s.open_key = k; }
        if let Ok(Value::Enum(i)) = cx.setting("pad_combo") { s.pad_combo = i; }
        // Take over the pause menu's "Mods" entry. Without the native menu adapter (or with this
        // mod unloaded) the game keeps its own Mods list; the key and pad combo always work.
        let mut entry = NativeNode::new(NodeKind::Action, NATIVE_OPEN, native::SYSTEM, "Mods");
        entry.help = "Installed mods and their settings".into();
        if let Err(e) = cx.native_menu_register(&entry) { cx.log_info(&format!("menu: pause-menu entry not replaced ({e})")); }
        let state = Rc::new(RefCell::new(s));
        let frame = state.clone();
        cx.on_frame_mut(move |cx| frame.borrow_mut().frame(cx))?;
        cx.log_info("Mod Manager ready");
        Ok(Self { state })
    }

    fn event(&mut self, cx: &mut Cx, event: &Event<'_>) {
        let mut s = self.state.borrow_mut();
        if let Some(n) = event.native_menu() {
            if n.change == Change::Activated && n.owner == "bbmod.manager" && n.key == NATIVE_OPEN && !s.open {
                s.set_open(cx, true);
            }
            return;
        }
        if let Some(c) = event.setting_changed() { s.apply_own_setting(&c.key, &c.value); return; }
        let events = s.screen.handle(event);
        for (key, kind) in events {
            if key == "browse-list" || key.starts_with("b-") {
                let mods = s.model.mods.clone();
                let outs = s.browse.on_event(&key, kind, &mods);
                s.apply(cx, outs);
                continue;
            }
            let opens_browse = matches!(&kind, EventKind::TabChanged(i) if *i == TAB_BROWSE);
            let commands = s.model.on_event(&key, kind);
            s.run(cx, commands);
            if opens_browse && !s.browse_started {
                s.browse_started = true;
                let outs = s.browse.refresh();
                s.apply(cx, outs);
            }
        }
    }

    fn unload(&mut self, cx: &mut Cx) {
        let mut s = self.state.borrow_mut();
        if let Some((_, _, t)) = &mut s.text { let _ = t.cancel(cx); }
        let _ = s.screen.close(cx);
    }
}

bbmod::export!(MenuMod);
