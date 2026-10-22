//! One interactive (or passive) tree with its focus: the per-layer logic shared by the
//! in-process `UiLayers` harness and the host-published `HostScreen`.

use super::geom::Rect;
use super::node::{render_node, DrawCmd, Kind, Measure, Node};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action { Up, Down, Left, Right, Confirm, Back, PrevTab, NextTab }

impl Action {
    /// From the host's `BBM_UI_ACT_*` value.
    pub fn from_host(v: u32) -> Option<Action> {
        use crate::sys::*;
        Some(match v {
            BBM_UI_ACT_UP => Action::Up, BBM_UI_ACT_DOWN => Action::Down, BBM_UI_ACT_LEFT => Action::Left,
            BBM_UI_ACT_RIGHT => Action::Right, BBM_UI_ACT_CONFIRM => Action::Confirm, BBM_UI_ACT_BACK => Action::Back,
            BBM_UI_ACT_PREV_TAB => Action::PrevTab, BBM_UI_ACT_NEXT_TAB => Action::NextTab, _ => return None,
        })
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum EventKind {
    Activated,
    Toggled(bool),
    ValueChanged(f32),
    ChoiceChanged(usize),
    /// A list's selection moved to this row key.
    Selected(String),
    /// Confirm on a list row.
    RowActivated(String),
    TabChanged(usize),
    /// Back was pressed; the owner decides whether to hide the layer.
    Back,
}

/// A node tree plus the key of the focused node. `interactive` trees keep focus on a
/// reachable focusable node; passive (HUD) trees never have focus.
#[derive(Clone, Debug)]
pub struct Screen {
    pub root: Node,
    focus: Option<String>,
    interactive: bool,
}

impl Screen {
    pub fn new(root: Node, interactive: bool) -> Self {
        let mut s = Self { root, focus: None, interactive };
        s.refocus();
        s
    }
    pub fn focus(&self) -> Option<&str> { self.focus.as_deref() }
    /// Move focus to `key` if it is currently reachable and focusable.
    pub fn set_focus(&mut self, key: &str) -> bool {
        let mut f = vec![];
        self.root.focusables(&mut f);
        if !self.interactive || !f.iter().any(|k| k == key) { return false; }
        self.focus = Some(key.to_owned());
        true
    }
    /// Mutate the tree, then keep focus on something reachable.
    pub fn update(&mut self, f: impl FnOnce(&mut Node)) {
        f(&mut self.root);
        self.refocus();
    }

    pub(crate) fn refocus(&mut self) {
        if !self.interactive { self.focus = None; return; }
        let mut f = vec![];
        self.root.focusables(&mut f);
        if self.focus.as_ref().is_some_and(|k| f.contains(k)) { return; }
        self.focus = f.into_iter().next();
    }
    fn move_focus(&mut self, forward: bool) {
        let mut f = vec![];
        self.root.focusables(&mut f);
        let Some(i) = self.focus.as_ref().and_then(|k| f.iter().position(|x| x == k)) else { return self.refocus() };
        let j = if forward { (i + 1).min(f.len() - 1) } else { i.saturating_sub(1) };
        self.focus = Some(f[j].clone());
    }

    /// Apply one semantic action; returns the resulting events (key = source node).
    pub fn input(&mut self, action: Action) -> Vec<(String, EventKind)> {
        let mut events = vec![];
        if !self.interactive { return events; }
        match action {
            Action::Back => events.push((self.root.key.clone(), EventKind::Back)),
            Action::PrevTab | Action::NextTab => {
                if let Some((key, idx)) = shift_tab(&mut self.root, action == Action::NextTab) {
                    events.push((key, EventKind::TabChanged(idx)));
                    self.refocus();
                }
            }
            Action::Up | Action::Down => {
                let forward = action == Action::Down;
                let Some(fk) = self.focus.clone() else { return events };
                let consumed = match self.root.find_mut(&fk).map(|n| &mut n.kind) {
                    Some(Kind::List(d)) => {
                        let moved = d.navigate(forward);
                        if moved { if let Some(k) = d.selected_key() { events.push((fk.clone(), EventKind::Selected(k.to_owned()))); } }
                        moved
                    }
                    _ => false,
                };
                if !consumed { self.move_focus(forward); }
            }
            Action::Left | Action::Right | Action::Confirm => {
                let Some(fk) = self.focus.clone() else { return events };
                let dir = match action { Action::Left => -1.0f32, Action::Right => 1.0, _ => 0.0 };
                if let Some(n) = self.root.find_mut(&fk) {
                    match &mut n.kind {
                        Kind::Button { .. } if action == Action::Confirm => events.push((fk, EventKind::Activated)),
                        Kind::Toggle { value, .. } if action == Action::Confirm || dir != 0.0 => {
                            let v = if dir == 0.0 { !*value } else { dir > 0.0 };
                            if v != *value { *value = v; events.push((fk, EventKind::Toggled(v))); }
                        }
                        Kind::Slider { value, min, max, step, .. } if dir != 0.0 => {
                            let v = (*value + dir * *step).clamp(*min, *max);
                            if v != *value { *value = v; events.push((fk, EventKind::ValueChanged(v))); }
                        }
                        Kind::Choice { choices, index, .. } if !choices.is_empty() && (dir != 0.0 || action == Action::Confirm) => {
                            let n = choices.len();
                            let i = if dir < 0.0 { (*index + n - 1) % n } else { (*index + 1) % n };
                            if i != *index { *index = i; events.push((fk, EventKind::ChoiceChanged(i))); }
                        }
                        Kind::List(d) if action == Action::Confirm => {
                            if let Some(k) = d.selected_key() { events.push((fk, EventKind::RowActivated(k.to_owned()))); }
                        }
                        _ => {}
                    }
                }
            }
        }
        events
    }

    /// Draw list in stage coordinates. `show_focus` is false while another layer owns input.
    pub fn draw(&self, bounds: Rect, show_focus: bool, alpha: f32, measure: &dyn Measure) -> Vec<DrawCmd> {
        let focus = if show_focus { self.focus.as_deref() } else { None };
        render_node(&self.root, bounds, focus, alpha, measure).0
    }
}

/// Move the first reachable tab strip; returns its key and new index when it changed.
fn shift_tab(n: &mut Node, forward: bool) -> Option<(String, usize)> {
    if !n.visible { return None; }
    let active = if let Kind::Tabs { labels, index } = &mut n.kind {
        let count = labels.len();
        let next = if forward { (*index + 1).min(count.saturating_sub(1)) } else { index.saturating_sub(1) };
        if next != *index {
            *index = next;
            return Some((n.key.clone(), next));
        }
        Some(*index)
    } else { None };
    n.children.iter_mut().enumerate()
        .filter(|(i, _)| active.is_none_or(|a| a == *i))
        .find_map(|(_, c)| shift_tab(c, forward))
}
