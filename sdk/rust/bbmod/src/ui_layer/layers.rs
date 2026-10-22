//! In-process layer manager: ownership, 60 Hz presence and one foreground input owner.
//! This is the reference model and test harness for the arbitration the host performs
//! for published layers (`ui_layers.c`). HUD layers are passive and never take input;
//! Screen and Modal layers compete for the foreground. Every handle carries a
//! generation so an action or update aimed at an unloaded or reloaded owner is rejected.

use super::clock::{Presence, UiClock};
use super::geom::{Canvas, Rect, STAGE_H, STAGE_W};
use super::node::{Align, DrawCmd, Measure, Node};
use super::screen::{Action, EventKind, Screen};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Handle { index: u32, generation: u64 }

/// Where a HUD layer sits on the stage.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Anchor { pub h: Align, pub v: Align, pub margin: f32, pub w: f32, pub height: f32 }
impl Anchor {
    pub fn rect(&self) -> Rect {
        let place = |a: Align, total: f32, size: f32| match a {
            Align::Start => self.margin,
            Align::Center => (total - size) / 2.0,
            Align::End => total - size - self.margin,
        };
        Rect::new(place(self.h, STAGE_W, self.w), place(self.v, STAGE_H, self.height), self.w, self.height)
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum LayerKind {
    /// Passive overlay: no focus, no input, drawn under screens.
    Hud(Anchor),
    /// Full-stage interactive screen.
    Screen,
    /// Interactive layer above screens, inside `bounds` (stage coordinates).
    Modal(Rect),
}
impl LayerKind {
    pub fn bounds(&self) -> Rect {
        match self { LayerKind::Hud(a) => a.rect(), LayerKind::Screen => Rect::STAGE, LayerKind::Modal(r) => *r }
    }
    pub fn interactive(&self) -> bool { !matches!(self, LayerKind::Hud(_)) }
}

#[derive(Clone, Debug, PartialEq)]
pub struct UiEvent { pub handle: Handle, pub key: String, pub kind: EventKind }

struct Layer {
    generation: u64,
    owner: String,
    kind: LayerKind,
    screen: Screen,
    presence: Presence,
}

#[derive(Default)]
pub struct UiLayers {
    slots: Vec<Option<Layer>>,
    next_generation: u64,
    clock: UiClock,
}

impl UiLayers {
    pub fn new() -> Self { Self::default() }
    pub fn clock(&self) -> &UiClock { &self.clock }

    /// Add a layer owned by `owner` (a mod id). It fades in immediately.
    pub fn register(&mut self, owner: &str, kind: LayerKind, root: Node) -> Handle {
        self.next_generation += 1;
        let mut presence = Presence::default();
        presence.show(self.clock.ticks());
        let layer = Layer { generation: self.next_generation, owner: owner.into(), kind,
            screen: Screen::new(root, kind.interactive()), presence };
        let index = match self.slots.iter().position(Option::is_none) {
            Some(i) => { self.slots[i] = Some(layer); i }
            None => { self.slots.push(Some(layer)); self.slots.len() - 1 }
        };
        Handle { index: index as u32, generation: self.next_generation }
    }
    fn layer(&mut self, h: Handle) -> Option<&mut Layer> {
        self.slots.get_mut(h.index as usize)?.as_mut().filter(|l| l.generation == h.generation)
    }
    pub fn is_live(&mut self, h: Handle) -> bool { self.layer(h).is_some() }
    /// Mutate a layer's tree. False if the handle is stale. Focus is repaired afterwards.
    pub fn update(&mut self, h: Handle, f: impl FnOnce(&mut Node)) -> bool {
        let Some(l) = self.layer(h) else { return false };
        l.screen.update(f);
        true
    }
    pub fn set_visible(&mut self, h: Handle, visible: bool) -> bool {
        let tick = self.clock.ticks();
        let Some(l) = self.layer(h) else { return false };
        if visible { l.presence.show(tick) } else { l.presence.hide(tick) }
        true
    }
    pub fn release(&mut self, h: Handle) -> bool {
        if self.layer(h).is_none() { return false; }
        self.slots[h.index as usize] = None;
        true
    }
    /// Remove every layer of `owner` (unload/reload). Their handles become stale.
    pub fn release_owner(&mut self, owner: &str) -> usize {
        let mut n = 0;
        for s in &mut self.slots {
            if s.as_ref().is_some_and(|l| l.owner == owner) { *s = None; n += 1; }
        }
        n
    }

    /// Advance the 60 Hz clock to `now_us`. Returns ticks elapsed.
    pub fn tick(&mut self, now_us: u64) -> u32 {
        let n = self.clock.advance(now_us);
        let t = self.clock.ticks();
        for l in self.slots.iter_mut().flatten() { l.presence.update(t); }
        n
    }

    /// The layer that currently receives input: the topmost drawn, wanted-visible
    /// interactive layer (Modal above Screen, later registration above earlier).
    pub fn foreground(&self) -> Option<Handle> {
        let pick = |modal: bool| self.slots.iter().enumerate().filter_map(|(i, s)| {
            let l = s.as_ref()?;
            let ok = l.presence.wants_visible() && match l.kind { LayerKind::Modal(_) => modal, LayerKind::Screen => !modal, LayerKind::Hud(_) => false };
            ok.then_some(Handle { index: i as u32, generation: l.generation })
        }).max_by_key(|h| h.generation);
        pick(true).or_else(|| pick(false))
    }

    /// True while an interactive layer should own the pad/keyboard.
    pub fn captures_input(&self) -> bool { self.foreground().is_some() }

    /// Apply one semantic action to the foreground layer.
    pub fn input(&mut self, action: Action) -> Vec<UiEvent> {
        let Some(h) = self.foreground() else { return vec![] };
        let l = self.layer(h).unwrap();
        l.screen.input(action).into_iter().map(|(key, kind)| UiEvent { handle: h, key, kind }).collect()
    }

    /// Draw list for every drawn layer in back-to-front order (HUD, Screen, Modal),
    /// in output pixels. The renderer supplies text metrics; the host supplies the
    /// canvas fit (see `Canvas::fit`).
    pub fn render(&self, canvas: &Canvas, measure: &dyn Measure) -> Vec<DrawCmd> {
        let tick = self.clock.ticks();
        let mut order: Vec<&Layer> = self.slots.iter().flatten().filter(|l| l.presence.is_drawn()).collect();
        order.sort_by_key(|l| (match l.kind { LayerKind::Hud(_) => 0, LayerKind::Screen => 1, LayerKind::Modal(_) => 2 }, l.generation));
        let fg = self.foreground().map(|h| h.generation);
        let mut out = vec![];
        for l in order {
            let cmds = l.screen.draw(l.kind.bounds(), fg == Some(l.generation), l.presence.alpha(tick), measure);
            out.extend(cmds.into_iter().map(|c| c.to_screen(canvas)));
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::super::node::{theme, Estimated, Kind, Layout, ListRow, Size};
    use super::*;

    fn menu() -> Node {
        Node::panel("root", Layout::Column)
            .child(Node::tabs("tabs", &["Mods", "Settings"]).size(Size::Fill, Size::Fill)
                .child(Node::container("mods", Layout::Column)
                    .child(Node::list("list", 3))
                    .child(Node::button("reload", "Reload")))
                .child(Node::container("settings", Layout::Column)
                    .child(Node::toggle("hud", "Show HUD", false))
                    .child(Node::slider("vol", "Volume", 0.5, 0.0, 1.0, 0.25))
                    .child(Node::choice("mode", "Mode", &["A", "B", "C"], 0))))
    }
    fn rows(n: usize) -> Vec<ListRow> { (0..n).map(|i| ListRow { key: format!("m{i}"), label: format!("Mod {i}") }).collect() }
    fn fill_list(l: &mut UiLayers, h: Handle) {
        assert!(l.update(h, |r| if let Kind::List(d) = &mut r.find_mut("list").unwrap().kind { d.set_rows(1, rows(5)).unwrap() }));
    }

    #[test]
    fn navigation_tabs_controls_and_back() {
        let mut ui = UiLayers::new();
        let h = ui.register("example.a", LayerKind::Screen, menu());
        fill_list(&mut ui, h);
        let ev = |ui: &mut UiLayers, a| ui.input(a).into_iter().map(|e| e.kind).collect::<Vec<_>>();
        assert_eq!(ev(&mut ui, Action::Down), [EventKind::Selected("m1".into())]);
        assert_eq!(ev(&mut ui, Action::Confirm), [EventKind::RowActivated("m1".into())]);
        for _ in 0..3 { ui.input(Action::Down); } // m2, m3, m4
        assert!(ev(&mut ui, Action::Down).is_empty()); // list end -> focus moves to the button
        assert_eq!(ev(&mut ui, Action::Confirm), [EventKind::Activated]);
        assert_eq!(ev(&mut ui, Action::NextTab), [EventKind::TabChanged(1)]);
        assert!(ev(&mut ui, Action::NextTab).is_empty()); // clamped at last tab
        assert_eq!(ev(&mut ui, Action::Confirm), [EventKind::Toggled(true)]);
        ui.input(Action::Down);
        assert_eq!(ev(&mut ui, Action::Right), [EventKind::ValueChanged(0.75)]);
        ui.input(Action::Down);
        assert_eq!(ev(&mut ui, Action::Left), [EventKind::ChoiceChanged(2)]);
        assert_eq!(ev(&mut ui, Action::Back), [EventKind::Back]);
        assert_eq!(ev(&mut ui, Action::PrevTab), [EventKind::TabChanged(0)]);
    }

    #[test]
    fn hud_never_captures_input_and_modal_is_foreground() {
        let mut ui = UiLayers::new();
        let anchor = Anchor { h: Align::End, v: Align::Start, margin: 24.0, w: 300.0, height: 60.0 };
        let hud = ui.register("example.hud", LayerKind::Hud(anchor), Node::bar("hp", 0.4, theme::ERROR));
        assert!(!ui.captures_input() && ui.input(Action::Confirm).is_empty());
        let screen = ui.register("example.a", LayerKind::Screen, menu());
        let modal = ui.register("example.b", LayerKind::Modal(Rect::new(600.0, 400.0, 720.0, 200.0)),
            Node::panel("dlg", Layout::Column).child(Node::button("ok", "OK")));
        assert_eq!(ui.foreground(), Some(modal));
        assert_eq!(ui.input(Action::Confirm)[0].key, "ok");
        assert!(ui.set_visible(modal, false));
        assert_eq!(ui.foreground(), Some(screen));
        assert!(ui.is_live(hud));
        assert_eq!(anchor.rect(), Rect::new(1596.0, 24.0, 300.0, 60.0));
    }

    #[test]
    fn stale_handles_are_rejected_after_unload_and_slots_are_reused_safely() {
        let mut ui = UiLayers::new();
        let a = ui.register("mod.a", LayerKind::Screen, menu());
        let b = ui.register("mod.b", LayerKind::Screen, menu());
        assert_eq!(ui.release_owner("mod.a"), 1);
        assert!(!ui.update(a, |_| {}) && !ui.set_visible(a, false) && !ui.release(a));
        let c = ui.register("mod.c", LayerKind::Screen, menu()); // reuses a's slot
        assert_ne!(a, c);
        assert!(!ui.update(a, |_| panic!("stale update ran")) && ui.update(c, |_| {}));
        assert_eq!(ui.foreground(), Some(c));
        assert!(ui.is_live(b));
    }

    #[test]
    fn focus_is_repaired_when_the_focused_node_goes_away() {
        let mut ui = UiLayers::new();
        let h = ui.register("m", LayerKind::Screen, menu());
        ui.update(h, |r| r.find_mut("reload").unwrap().visible = false);
        assert!(ui.input(Action::Confirm).is_empty()); // list is focused; empty list has nothing to activate
        ui.update(h, |r| r.find_mut("list").unwrap().enabled = false);
        assert!(ui.input(Action::Confirm).is_empty()); // nothing focusable, no panic
    }

    #[test]
    fn render_fades_in_scales_to_output_and_runs_at_60hz() {
        let mut ui = UiLayers::new();
        let canvas = Canvas::fit((3840.0, 2160.0), (1920.0, 1080.0)).unwrap();
        ui.tick(0);
        ui.register("m", LayerKind::Screen, Node::panel("p", Layout::Column).child(Node::title("t", "Hello")));
        let alpha_of = |ui: &UiLayers| match ui.render(&canvas, &Estimated).iter().find_map(|c| match c {
            DrawCmd::Text { color, .. } => Some(color.0 & 0xff), _ => None }) { Some(a) => a, None => 0 };
        assert_eq!(alpha_of(&ui), 0);
        for i in 1..=6 { ui.tick(i * 16_667); }
        let mid = alpha_of(&ui);
        assert!(mid > 0 && mid < 0xff, "{mid}");
        for i in 7..=20 { ui.tick(i * 16_667); }
        assert_eq!(alpha_of(&ui), 0xff);
        let size = ui.render(&canvas, &Estimated).iter().find_map(|c| match c { DrawCmd::Text { size, .. } => Some(*size), _ => None }).unwrap();
        assert_eq!(size, 48.0); // 24 px at 4K: scale 2
    }
}
