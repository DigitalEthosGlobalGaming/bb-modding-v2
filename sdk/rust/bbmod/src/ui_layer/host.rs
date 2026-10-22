//! Publish a `Screen` to the host as an owned layer (ABI 1.11) and route its input.
//!
//! The host draws the published list, owns the 60 Hz fade, scaling, and the single
//! foreground input owner. This type holds only copies: no host pointer or callback
//! is stored in the host, and the layer is released with the mod.

use super::encode::encode;
use super::geom::Rect;
use super::layers::LayerKind;
use super::node::{Estimated, Measure, Node};
use super::screen::{Action, EventKind, Screen};
use crate::{check, sys, Cx, Error, Event, Id, Result};

/// Subscribe with this (OR'd with other masks) to receive input for your layers.
pub const EVENTS: u32 = sys::BBM_EV_UI_ACTION;

pub struct HostScreen {
    kind: LayerKind,
    order: i32,
    screen: Screen,
    id: Option<Id>,
    visible: bool,
    active: bool,
    last: Option<(Vec<u8>, bool)>,
    measure: Box<dyn Measure>,
}

impl HostScreen {
    /// Not published until `publish`. `order` ranks layers of the same kind.
    pub fn new(kind: LayerKind, order: i32, root: Node) -> Self {
        Self { kind, order, screen: Screen::new(root, kind.interactive()), id: None, visible: false, active: true,
            last: None, measure: Box::new(Estimated) }
    }
    pub fn screen(&self) -> &Screen { &self.screen }
    pub fn root(&self) -> &Node { &self.screen.root }
    pub fn id(&self) -> Option<Id> { self.id }
    pub fn is_visible(&self) -> bool { self.visible }
    pub fn set_measure(&mut self, m: Box<dyn Measure>) { self.measure = m; }

    /// Mutate the tree; the next `publish` sends it if the drawing changed.
    pub fn update(&mut self, f: impl FnOnce(&mut Node)) { self.screen.update(f); }
    pub fn set_focus(&mut self, key: &str) -> bool { self.screen.set_focus(key) }
    pub fn focus(&self) -> Option<&str> { self.screen.focus() }
    /// Show or hide (fades). Hidden layers neither draw nor capture input.
    pub fn set_visible(&mut self, visible: bool) { self.visible = visible; }
    /// False hides the focus highlight (e.g. while a modal above owns input).
    pub fn set_active(&mut self, active: bool) { self.active = active; }

    /// Send the current drawing if it changed since the last publish.
    pub fn publish(&mut self, cx: &mut Cx) -> Result<()> {
        let h = unsafe { &*cx.host };
        if !h.has_ui_layers() { return Err(Error::UNSUPPORTED); }
        let bounds = self.kind.bounds();
        let bytes = encode(&self.screen.draw(bounds, self.active, 1.0, self.measure.as_ref()));
        if self.id.is_some() && self.last.as_ref().is_some_and(|(b, v)| *b == bytes && *v == self.visible) { return Ok(()); }
        let kind = match self.kind { LayerKind::Hud(_) => sys::BBM_UI_HUD, LayerKind::Screen => sys::BBM_UI_SCREEN, LayerKind::Modal(_) => sys::BBM_UI_MODAL };
        let r = if let LayerKind::Modal(r) = self.kind { r } else { Rect::default() };
        let desc = sys::bbm_ui_layer { size: core::mem::size_of::<sys::bbm_ui_layer>() as u32, kind,
            flags: if self.visible { sys::BBM_UI_VISIBLE } else { 0 }, order: self.order, x: r.x, y: r.y, w: r.w, h: r.h };
        let mut id = self.id.map_or(0, |i| i.0);
        check(unsafe { (h.ui_layer_set)(cx.me, &mut id, &desc, bytes.as_ptr().cast(), bytes.len()) })?;
        self.id = Some(Id(id));
        self.last = Some((bytes, self.visible));
        Ok(())
    }

    /// Release the host layer (also happens automatically on unload).
    pub fn close(&mut self, cx: &Cx) -> Result<()> {
        if let Some(id) = self.id.take() { cx.release(id)?; }
        self.last = None;
        Ok(())
    }

    /// Apply a host input event aimed at this layer. Returns the widget events it caused;
    /// the tree changed, so call `publish` afterwards.
    pub fn handle(&mut self, event: &Event) -> Vec<(String, EventKind)> {
        if event.kind != sys::BBM_EV_UI_ACTION || event.data.len() != core::mem::size_of::<sys::bbm_ui_action>() { return vec![]; }
        // SAFETY: length checked above; read_unaligned copes with the borrowed slice's alignment.
        let a = unsafe { core::ptr::read_unaligned(event.data.as_ptr().cast::<sys::bbm_ui_action>()) };
        if a.size as usize != event.data.len() || Some(Id(a.layer)) != self.id { return vec![]; }
        match Action::from_host(a.action) { Some(act) => self.screen.input(act), None => vec![] }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn host_layouts_match_the_c_header() {
        assert_eq!(core::mem::size_of::<sys::bbm_ui_layer>(), 32);
        assert_eq!(core::mem::size_of::<sys::bbm_ui_action>(), 16);
    }
    #[test]
    fn foreign_or_malformed_events_are_ignored() {
        use super::super::node::Layout;
        let mut s = HostScreen::new(LayerKind::Screen, 0, Node::panel("r", Layout::Column).child(Node::button("b", "B")));
        s.id = Some(Id(7));
        let act = |layer: u64, action: u32| sys::bbm_ui_action { size: 16, action, layer };
        let bytes = |a: &sys::bbm_ui_action| unsafe { core::slice::from_raw_parts((a as *const sys::bbm_ui_action).cast::<u8>(), 16) }.to_vec();
        fn ev(data: &[u8]) -> Event<'_> { Event { kind: sys::BBM_EV_UI_ACTION, a: 0, b: 0, data } }
        let mine = bytes(&act(7, sys::BBM_UI_ACT_CONFIRM));
        assert_eq!(s.handle(&ev(&mine)), [("b".to_string(), EventKind::Activated)]);
        assert!(s.handle(&ev(&bytes(&act(8, sys::BBM_UI_ACT_CONFIRM)))).is_empty());
        assert!(s.handle(&ev(&bytes(&act(7, 99)))).is_empty());
        assert!(s.handle(&ev(&mine[..8])).is_empty());
    }
}
