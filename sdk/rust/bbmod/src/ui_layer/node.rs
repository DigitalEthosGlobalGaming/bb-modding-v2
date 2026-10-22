//! Retained component tree, layout and draw-list generation. Renderer independent:
//! `render_node` turns a tree into `DrawCmd`s in stage coordinates (1920x1080).

use super::geom::Rect;
use crate::ui_state::{ListState, SnapshotError};
use std::num::NonZeroUsize;

/// 0xRRGGBBAA.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Color(pub u32);
impl Color {
    pub const fn rgb(v: u32) -> Self { Self(v << 8 | 0xff) }
    pub fn with_alpha(self, a: f32) -> Self {
        let base = (self.0 & 0xff) as f32;
        Self((self.0 & !0xff) | (base * a.clamp(0.0, 1.0)).round() as u32)
    }
}

/// Tokens measured from the game's own menus (text-field colours and sizes across the
/// surveyed movies). Not a claim that every native screen uses exactly these.
pub mod theme {
    use super::Color;
    pub const PRIMARY: Color = Color::rgb(0xa5a58c);
    pub const SECONDARY: Color = Color::rgb(0xa6a397);
    pub const ACCENT: Color = Color::rgb(0xa0824b);
    pub const BRIGHT: Color = Color::rgb(0xcccccc);
    pub const WHITE: Color = Color::rgb(0xffffff);
    pub const DISABLED: Color = Color::rgb(0x505050);
    pub const ERROR: Color = Color::rgb(0xd02f2f);
    pub const PANEL: Color = Color(0x000000b0);
    pub const BODY_SIZE: f32 = 22.0;
    pub const TITLE_SIZE: f32 = 24.0;
    pub const ROW_H: f32 = 36.0;
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum Align { #[default] Start, Center, End }

#[derive(Clone, Debug, PartialEq)]
pub enum DrawCmd {
    Fill { rect: Rect, color: Color },
    Stroke { rect: Rect, color: Color, width: f32 },
    Text { rect: Rect, text: String, size: f32, color: Color, align: Align, bold: bool },
    /// `name` is a game export name (e.g. `KG_OK`); the renderer resolves it.
    Image { rect: Rect, name: String, tint: Color },
}

impl DrawCmd {
    /// Map stage coordinates to output pixels.
    pub fn to_screen(self, c: &super::geom::Canvas) -> DrawCmd {
        match self {
            DrawCmd::Fill { rect, color } => DrawCmd::Fill { rect: c.to_screen(rect), color },
            DrawCmd::Stroke { rect, color, width } => DrawCmd::Stroke { rect: c.to_screen(rect), color, width: width * c.scale },
            DrawCmd::Text { rect, text, size, color, align, bold } =>
                DrawCmd::Text { rect: c.to_screen(rect), text, size: size * c.scale, color, align, bold },
            DrawCmd::Image { rect, name, tint } => DrawCmd::Image { rect: c.to_screen(rect), name, tint },
        }
    }
}

/// Text measurement supplied by the renderer. The default is a rough estimate only.
pub trait Measure { fn text_width(&self, text: &str, size: f32) -> f32; }
pub struct Estimated;
impl Measure for Estimated {
    fn text_width(&self, text: &str, size: f32) -> f32 { text.chars().count() as f32 * size * 0.55 }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Size { Auto, Fill, Fixed(f32) }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Layout { Column, Row, Layer }

#[derive(Clone, Debug, PartialEq)]
pub struct ListRow { pub key: String, pub label: String }

/// Rows plus selection/viewport. Rows are copied in; the state is the SDK's `ListState`.
#[derive(Clone, Debug)]
pub struct ListData { rows: Vec<ListRow>, state: ListState<String> }
impl ListData {
    pub fn new(visible_rows: usize) -> Self {
        Self { rows: vec![], state: ListState::new(NonZeroUsize::new(visible_rows.max(1)).unwrap()) }
    }
    /// Atomically replace rows with a newer revision (see `ListState::replace`).
    pub fn set_rows(&mut self, revision: u64, rows: Vec<ListRow>) -> Result<(), SnapshotError> {
        self.state.replace(revision, rows.iter().map(|r| r.key.clone()).collect())?;
        self.rows = rows;
        Ok(())
    }
    pub fn selected_key(&self) -> Option<&str> { self.state.selected_key().map(String::as_str) }
    pub fn selected_row(&self) -> Option<&ListRow> {
        self.state.selected_index().and_then(|i| self.rows.get(i))
    }
    pub fn revision(&self) -> u64 { self.state.revision() }
    /// Select a row by key (no-op if absent). True if the selection changed.
    pub fn select(&mut self, key: &str) -> bool {
        let rev = self.state.revision();
        self.state.select(rev, &key.to_owned()).unwrap_or(false)
    }
    pub fn rows(&self) -> &[ListRow] { &self.rows }
    pub(crate) fn navigate(&mut self, forward: bool) -> bool { self.state.navigate(forward) }
    pub fn len(&self) -> usize { self.rows.len() }
    pub fn is_empty(&self) -> bool { self.rows.is_empty() }
}

#[derive(Clone, Debug)]
pub enum Kind {
    Container,
    Panel,
    Text { text: String, size: f32, color: Color, align: Align, bold: bool },
    Icon { name: String, tint: Color },
    Bar { value: f32, color: Color },
    Button { label: String },
    Toggle { label: String, value: bool },
    Slider { label: String, value: f32, min: f32, max: f32, step: f32 },
    Choice { label: String, choices: Vec<String>, index: usize },
    List(ListData),
    /// children[index] is the content; tab_labels name each child. L1/R1 move `index`.
    Tabs { labels: Vec<String>, index: usize },
    /// Prompt icon name and caption pairs, right aligned.
    KeyGuide { prompts: Vec<(String, String)> },
}

#[derive(Clone, Debug)]
pub struct Node {
    pub key: String,
    pub kind: Kind,
    pub children: Vec<Node>,
    pub width: Size,
    pub height: Size,
    pub layout: Layout,
    pub gap: f32,
    pub padding: f32,
    pub visible: bool,
    pub enabled: bool,
}

impl Node {
    fn base(key: &str, kind: Kind) -> Self {
        Self { key: key.into(), kind, children: vec![], width: Size::Auto, height: Size::Auto,
            layout: Layout::Column, gap: 0.0, padding: 0.0, visible: true, enabled: true }
    }
    pub fn container(key: &str, layout: Layout) -> Self { Self { layout, ..Self::base(key, Kind::Container) } }
    pub fn panel(key: &str, layout: Layout) -> Self { Self { layout, padding: 16.0, gap: 8.0, ..Self::base(key, Kind::Panel) } }
    pub fn text(key: &str, text: &str) -> Self {
        Self::base(key, Kind::Text { text: text.into(), size: theme::BODY_SIZE, color: theme::PRIMARY, align: Align::Start, bold: false })
    }
    pub fn title(key: &str, text: &str) -> Self {
        Self::base(key, Kind::Text { text: text.into(), size: theme::TITLE_SIZE, color: theme::PRIMARY, align: Align::Center, bold: true })
    }
    pub fn icon(key: &str, name: &str, w: f32, h: f32) -> Self {
        Self { width: Size::Fixed(w), height: Size::Fixed(h), ..Self::base(key, Kind::Icon { name: name.into(), tint: theme::WHITE }) }
    }
    pub fn bar(key: &str, value: f32, color: Color) -> Self {
        Self { height: Size::Fixed(12.0), ..Self::base(key, Kind::Bar { value, color }) }
    }
    pub fn button(key: &str, label: &str) -> Self { Self::base(key, Kind::Button { label: label.into() }) }
    pub fn toggle(key: &str, label: &str, value: bool) -> Self { Self::base(key, Kind::Toggle { label: label.into(), value }) }
    pub fn slider(key: &str, label: &str, value: f32, min: f32, max: f32, step: f32) -> Self {
        Self::base(key, Kind::Slider { label: label.into(), value: value.clamp(min, max), min, max, step })
    }
    pub fn choice(key: &str, label: &str, choices: &[&str], index: usize) -> Self {
        let choices: Vec<String> = choices.iter().map(|s| s.to_string()).collect();
        let index = index.min(choices.len().saturating_sub(1));
        Self::base(key, Kind::Choice { label: label.into(), choices, index })
    }
    pub fn list(key: &str, visible_rows: usize) -> Self { Self::base(key, Kind::List(ListData::new(visible_rows))) }
    pub fn tabs(key: &str, labels: &[&str]) -> Self {
        Self::base(key, Kind::Tabs { labels: labels.iter().map(|s| s.to_string()).collect(), index: 0 })
    }
    pub fn key_guide(key: &str, prompts: &[(&str, &str)]) -> Self {
        Self { height: Size::Fixed(36.0), ..Self::base(key, Kind::KeyGuide {
            prompts: prompts.iter().map(|(a, b)| (a.to_string(), b.to_string())).collect() }) }
    }
    pub fn child(mut self, n: Node) -> Self { self.children.push(n); self }
    pub fn size(mut self, w: Size, h: Size) -> Self { self.width = w; self.height = h; self }
    pub fn gap(mut self, g: f32) -> Self { self.gap = g; self }
    pub fn padding(mut self, p: f32) -> Self { self.padding = p; self }
    pub fn enabled(mut self, e: bool) -> Self { self.enabled = e; self }

    pub fn is_focusable(&self) -> bool {
        self.visible && self.enabled
            && matches!(self.kind, Kind::Button { .. } | Kind::Toggle { .. } | Kind::Slider { .. } | Kind::Choice { .. } | Kind::List(_))
    }
    /// Children that are laid out and reachable: all, except only the active tab.
    pub(crate) fn live_children(&self) -> Vec<&Node> {
        match &self.kind {
            Kind::Tabs { index, .. } => self.children.get(*index).into_iter().collect(),
            _ => self.children.iter().collect(),
        }
    }
    pub fn find_mut(&mut self, key: &str) -> Option<&mut Node> {
        if self.key == key { return Some(self); }
        self.children.iter_mut().find_map(|c| c.find_mut(key))
    }
    pub fn find(&self, key: &str) -> Option<&Node> {
        if self.key == key { return Some(self); }
        self.children.iter().find_map(|c| c.find(key))
    }
    /// Focus order: pre-order over visible, reachable nodes. First duplicate key wins.
    pub fn focusables(&self, out: &mut Vec<String>) {
        if !self.visible { return; }
        if self.is_focusable() && !out.contains(&self.key) { out.push(self.key.clone()); }
        for c in self.live_children() { c.focusables(out); }
    }
}

fn preferred(n: &Node, m: &dyn Measure) -> (f32, f32) {
    let (w, h) = match &n.kind {
        Kind::Text { text, size, .. } => (m.text_width(text, *size), size * 1.35),
        Kind::Icon { .. } => (32.0, 32.0),
        Kind::Bar { .. } => (200.0, 12.0),
        Kind::Button { .. } | Kind::Toggle { .. } | Kind::Slider { .. } | Kind::Choice { .. } => (420.0, theme::ROW_H),
        Kind::List(d) => (480.0, theme::ROW_H * d.state.visible_range().len().max(1).max(
            d.rows.len().min(7)) as f32),
        Kind::KeyGuide { .. } => (0.0, 36.0),
        Kind::Tabs { .. } => {
            let (cw, ch) = n.live_children().iter().fold((0.0f32, 0.0f32), |a, c| {
                let p = preferred(c, m); (a.0.max(p.0), a.1.max(p.1)) });
            (cw, ch + theme::ROW_H)
        }
        Kind::Container | Kind::Panel => {
            let kids = n.live_children().into_iter().filter(|c| c.visible).collect::<Vec<_>>();
            let gaps = n.gap * kids.len().saturating_sub(1) as f32;
            let (mut main, mut cross) = (0.0f32, 0.0f32);
            for c in &kids {
                let p = preferred(c, m);
                let (cm, cc) = match n.layout { Layout::Row => (p.0, p.1), _ => (p.1, p.0) };
                match n.layout { Layout::Layer => { main = main.max(cm); } _ => { main += cm; } }
                cross = cross.max(cc);
            }
            match n.layout {
                Layout::Row => (main + gaps, cross),
                Layout::Column => (cross, main + gaps),
                Layout::Layer => (cross, main),
            }
        }
    };
    let pad = 2.0 * n.padding;
    let (w, h) = if matches!(n.kind, Kind::Container | Kind::Panel) { (w + pad, h + pad) } else { (w, h) };
    (match n.width { Size::Fixed(v) => v, _ => w }, match n.height { Size::Fixed(v) => v, _ => h })
}

struct Ctx<'a> {
    alpha: f32,
    focus: Option<&'a str>,
    measure: &'a dyn Measure,
    out: Vec<DrawCmd>,
    boxes: Vec<(String, Rect)>,
}
impl Ctx<'_> {
    fn fill(&mut self, rect: Rect, color: Color) { self.out.push(DrawCmd::Fill { rect, color: color.with_alpha(self.alpha) }); }
    fn stroke(&mut self, rect: Rect, color: Color) { self.out.push(DrawCmd::Stroke { rect, color: color.with_alpha(self.alpha), width: 2.0 }); }
    fn text(&mut self, rect: Rect, text: &str, size: f32, color: Color, align: Align, bold: bool) {
        self.out.push(DrawCmd::Text { rect, text: text.into(), size, color: color.with_alpha(self.alpha), align, bold });
    }
}

fn place(children: &[&Node], node: &Node, inner: Rect, m: &dyn Measure) -> Vec<Rect> {
    if node.layout == Layout::Layer {
        return children.iter().map(|c| {
            let p = preferred(c, m);
            let w = if c.width == Size::Fill { inner.w } else { p.0.min(inner.w) };
            let h = if c.height == Size::Fill { inner.h } else { p.1.min(inner.h) };
            Rect::new(inner.x, inner.y, w, h)
        }).collect();
    }
    let row = node.layout == Layout::Row;
    let main_of = |c: &Node| if row { c.width } else { c.height };
    let total = if row { inner.w } else { inner.h } - node.gap * children.len().saturating_sub(1) as f32;
    let mut used = 0.0;
    let mut fills = 0;
    let sizes: Vec<Option<f32>> = children.iter().map(|c| match main_of(c) {
        Size::Fill => { fills += 1; None }
        _ => { let p = preferred(c, m); let v = if row { p.0 } else { p.1 }; used += v; Some(v) }
    }).collect();
    let share = if fills > 0 { ((total - used) / fills as f32).max(0.0) } else { 0.0 };
    let mut pos = if row { inner.x } else { inner.y };
    children.iter().zip(sizes).map(|(c, s)| {
        let main = s.unwrap_or(share);
        let p = preferred(c, m);
        let cross_size = if row { c.height } else { c.width };
        let cross_avail = if row { inner.h } else { inner.w };
        let cross = match cross_size { Size::Fill => cross_avail, _ => (if row { p.1 } else { p.0 }).min(cross_avail) };
        let r = if row { Rect::new(pos, inner.y, main, cross) } else { Rect::new(inner.x, pos, cross, main) };
        pos += main + node.gap;
        r
    }).collect()
}

fn draw(n: &Node, rect: Rect, cx: &mut Ctx) {
    if !n.visible { return; }
    cx.boxes.push((n.key.clone(), rect));
    let focused = cx.focus == Some(n.key.as_str());
    let tint = if n.enabled { theme::PRIMARY } else { theme::DISABLED };
    let row_chrome = |cx: &mut Ctx| if focused {
        cx.fill(rect, theme::ACCENT.with_alpha(0.25));
        cx.stroke(rect, theme::ACCENT);
    };
    let label_color = if !n.enabled { theme::DISABLED } else if focused { theme::BRIGHT } else { tint };
    let pad = Rect::new(rect.x + 12.0, rect.y, (rect.w - 24.0).max(0.0), rect.h);
    match &n.kind {
        Kind::Container | Kind::Panel => {
            if matches!(n.kind, Kind::Panel) {
                cx.fill(rect, theme::PANEL);
                cx.stroke(rect, theme::ACCENT.with_alpha(0.5));
            }
            let kids: Vec<&Node> = n.live_children().into_iter().filter(|c| c.visible).collect();
            for (c, r) in kids.iter().zip(place(&kids, n, rect.inset(n.padding), cx.measure)) { draw(c, r, cx); }
        }
        Kind::Text { text, size, color, align, bold } => cx.text(rect, text, *size, *color, *align, *bold),
        Kind::Icon { name, tint } => cx.out.push(DrawCmd::Image { rect, name: name.clone(), tint: tint.with_alpha(cx.alpha) }),
        Kind::Bar { value, color } => {
            cx.fill(rect, theme::DISABLED.with_alpha(0.6));
            cx.fill(Rect::new(rect.x, rect.y, rect.w * value.clamp(0.0, 1.0), rect.h), *color);
        }
        Kind::Button { label } => {
            row_chrome(cx);
            cx.text(pad, label, theme::BODY_SIZE, label_color, Align::Start, false);
        }
        Kind::Toggle { label, value } => {
            row_chrome(cx);
            cx.text(pad, label, theme::BODY_SIZE, label_color, Align::Start, false);
            cx.text(pad, if *value { "On" } else { "Off" }, theme::BODY_SIZE, label_color, Align::End, false);
        }
        Kind::Slider { label, value, min, max, step } => {
            row_chrome(cx);
            cx.text(pad, label, theme::BODY_SIZE, label_color, Align::Start, false);
            let track = Rect::new(rect.x + rect.w * 0.55, rect.y + rect.h / 2.0 - 3.0, rect.w * 0.3, 6.0);
            let t = if max > min { (value - min) / (max - min) } else { 0.0 };
            cx.fill(track, theme::DISABLED.with_alpha(0.6));
            cx.fill(Rect::new(track.x, track.y, track.w * t.clamp(0.0, 1.0), track.h), if focused { theme::BRIGHT } else { theme::ACCENT });
            let shown = if value.fract() == 0.0 && step.fract() == 0.0 { format!("{value:.0}") } else { format!("{value:.2}") };
            cx.text(pad, &shown, theme::BODY_SIZE, label_color, Align::End, false);
        }
        Kind::Choice { label, choices, index } => {
            row_chrome(cx);
            cx.text(pad, label, theme::BODY_SIZE, label_color, Align::Start, false);
            cx.text(pad, choices.get(*index).map(String::as_str).unwrap_or(""), theme::BODY_SIZE, label_color, Align::End, false);
        }
        Kind::List(d) => {
            let range = d.state.visible_range();
            for (slot, i) in range.clone().enumerate() {
                let r = Rect::new(rect.x, rect.y + slot as f32 * theme::ROW_H, rect.w - 14.0, theme::ROW_H);
                let selected = d.state.selected_index() == Some(i);
                if selected {
                    cx.fill(r, theme::ACCENT.with_alpha(if focused { 0.3 } else { 0.12 }));
                    if focused { cx.stroke(r, theme::ACCENT); }
                }
                let color = if !n.enabled { theme::DISABLED } else if selected { theme::BRIGHT } else { theme::PRIMARY };
                cx.text(Rect::new(r.x + 12.0, r.y, r.w - 24.0, r.h), &d.rows[i].label, theme::BODY_SIZE, color, Align::Start, false);
            }
            if d.len() > range.len() {
                let track = Rect::new(rect.right() - 8.0, rect.y, 6.0, theme::ROW_H * range.len() as f32);
                cx.fill(track, theme::DISABLED.with_alpha(0.5));
                let thumb_h = track.h * range.len() as f32 / d.len() as f32;
                let thumb_y = track.y + (track.h - thumb_h) * range.start as f32 / (d.len() - range.len()) as f32;
                cx.fill(Rect::new(track.x, thumb_y, track.w, thumb_h), theme::ACCENT);
            }
        }
        Kind::Tabs { labels, index } => {
            let head_h = theme::ROW_H;
            let cell = if labels.is_empty() { 0.0 } else { (rect.w - 120.0) / labels.len() as f32 };
            cx.out.push(DrawCmd::Image { rect: Rect::new(rect.x, rect.y + 2.0, 32.0, 32.0), name: "KG_L1".into(), tint: theme::WHITE.with_alpha(cx.alpha) });
            cx.out.push(DrawCmd::Image { rect: Rect::new(rect.right() - 32.0, rect.y + 2.0, 32.0, 32.0), name: "KG_R1".into(), tint: theme::WHITE.with_alpha(cx.alpha) });
            for (i, l) in labels.iter().enumerate() {
                let r = Rect::new(rect.x + 60.0 + cell * i as f32, rect.y, cell, head_h);
                let active = i == *index;
                cx.text(r, l, theme::BODY_SIZE, if active { theme::BRIGHT } else { theme::SECONDARY }, Align::Center, active);
                if active { cx.fill(Rect::new(r.x + 8.0, r.bottom() - 3.0, (r.w - 16.0).max(0.0), 2.0), theme::ACCENT); }
            }
            let body = Rect::new(rect.x, rect.y + head_h, rect.w, (rect.h - head_h).max(0.0));
            for c in n.live_children() { draw(c, body, cx); }
        }
        Kind::KeyGuide { prompts } => {
            let mut x = rect.right();
            for (icon, caption) in prompts.iter().rev() {
                let w = cx.measure.text_width(caption, theme::BODY_SIZE);
                x -= w;
                cx.text(Rect::new(x, rect.y, w, rect.h), caption, theme::BODY_SIZE, theme::PRIMARY, Align::Start, false);
                x -= 36.0;
                cx.out.push(DrawCmd::Image { rect: Rect::new(x, rect.y + 2.0, 32.0, 32.0), name: icon.clone(), tint: theme::WHITE.with_alpha(cx.alpha) });
                x -= 24.0;
            }
        }
    }
}

/// Lay out `root` into `bounds` (stage coordinates) and return the draw list and
/// each visible node's rectangle (for hit testing). `alpha` multiplies every colour.
pub fn render_node(root: &Node, bounds: Rect, focus: Option<&str>, alpha: f32, measure: &dyn Measure)
    -> (Vec<DrawCmd>, Vec<(String, Rect)>)
{
    let mut cx = Ctx { alpha, focus, measure, out: vec![], boxes: vec![] };
    draw(root, bounds, &mut cx);
    (cx.out, cx.boxes)
}

#[cfg(test)]
mod tests {
    use super::*;
    fn screen() -> Node {
        Node::panel("root", Layout::Column)
            .child(Node::title("title", "Mods"))
            .child(Node::tabs("tabs", &["Mods", "Settings"])
                .size(Size::Fill, Size::Fill)
                .child(Node::container("mods", Layout::Row).gap(16.0)
                    .child(Node::list("list", 7).size(Size::Fixed(480.0), Size::Fill))
                    .child(Node::text("detail", "Details").size(Size::Fill, Size::Fill)))
                .child(Node::toggle("t", "Hidden tab", true)))
            .child(Node::key_guide("guide", &[("KG_OK", "Select"), ("KG_Cancel", "Back")]))
    }
    #[test]
    fn layout_fills_remaining_space_and_only_draws_the_active_tab() {
        let (cmds, boxes) = render_node(&screen(), Rect::new(100.0, 100.0, 1000.0, 600.0), None, 1.0, &Estimated);
        let get = |k: &str| boxes.iter().find(|(n, _)| n == k).unwrap().1;
        let list = get("list");
        assert_eq!((list.w, list.x), (480.0, 100.0 + 16.0));
        assert_eq!(get("detail").x, list.right() + 16.0);
        assert!(get("guide").bottom() <= 700.0 - 16.0 + 0.01 && get("tabs").bottom() <= get("guide").y + 0.01);
        assert!(boxes.iter().all(|(k, _)| k != "t"));
        assert!(cmds.iter().any(|c| matches!(c, DrawCmd::Image { name, .. } if name == "KG_L1")));
    }
    #[test]
    fn alpha_scales_every_colour_and_lists_window_rows() {
        let mut root = Node::list("l", 3).size(Size::Fixed(300.0), Size::Fixed(108.0));
        if let Kind::List(d) = &mut root.kind {
            d.set_rows(1, (0..10).map(|i| ListRow { key: format!("r{i}"), label: format!("Row {i}") }).collect()).unwrap();
        }
        let (cmds, _) = render_node(&root, Rect::new(0.0, 0.0, 300.0, 108.0), Some("l"), 0.5, &Estimated);
        let texts = cmds.iter().filter(|c| matches!(c, DrawCmd::Text { .. })).count();
        assert_eq!(texts, 3);
        assert!(cmds.iter().all(|c| match c { DrawCmd::Text { color, .. } | DrawCmd::Fill { color, .. } => color.0 & 0xff <= 0x80, _ => true }));
    }
    #[test]
    fn focus_order_skips_disabled_hidden_and_inactive_tabs() {
        let mut s = screen();
        s.find_mut("list").unwrap().enabled = false;
        let mut f = vec![];
        s.focusables(&mut f);
        assert!(f.is_empty());
        s.find_mut("list").unwrap().enabled = true;
        f.clear();
        s.focusables(&mut f);
        assert_eq!(f, ["list"]);
    }
}
