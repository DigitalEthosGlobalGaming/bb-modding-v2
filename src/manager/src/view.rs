//! Widget tree for the menu. `build` makes the static structure once; `View::sync`
//! then updates it in place from the model so list selection and focus persist.

use crate::browse::{Browse, ManifestState};
use crate::catalog::{compat, compat_text, HOST_ABI};
use crate::model::{key_options, state_mark, state_text, Model, CONTROL};
use bbmod::settings::{Kind as SKind, ModInfo, ModState, Value};
use bbmod::ui_layer::{theme, Kind, Layout, ListRow, Node, Size};

const DESC_LINES: usize = 6;
const LIST_ROWS: usize = 12;

pub fn build() -> Node {
    let mut detail = Node::panel("detail", Layout::Column).size(Size::Fill, Size::Fill).gap(6.0)
        .child(Node::title("d-name", "").size(Size::Fill, Size::Auto))
        .child(Node::text("d-meta", "")).child(Node::text("d-state", ""));
    for i in 0..DESC_LINES { detail = detail.child(Node::text(&format!("d-desc-{i}"), "")); }
    detail = detail.child(Node::text("d-order", "")).child(Node::text("d-error", ""))
        .child(Node::container("spacer", Layout::Column).size(Size::Fill, Size::Fill))
        .child(Node::button("toggle", "Unload"))
        .child(Node::button("reload", "Reload"))
        .child(Node::button("uninstall", "Uninstall"));

    let mut bdetail = Node::panel("bdetail", Layout::Column).size(Size::Fill, Size::Fill).gap(6.0)
        .child(Node::title("b-name", "").size(Size::Fill, Size::Auto))
        .child(Node::text("b-meta", "")).child(Node::text("b-compat", ""));
    for i in 0..DESC_LINES { bdetail = bdetail.child(Node::text(&format!("b-desc-{i}"), "")); }
    bdetail = bdetail.child(Node::text("b-source", ""))
        .child(Node::container("bspacer", Layout::Column).size(Size::Fill, Size::Fill))
        .child(Node::button("b-install", "Install")).child(Node::button("b-remove", "Remove"))
        .child(Node::button("b-remove-source", "Remove this mod's list"))
        .child(Node::button("b-refresh", "Refresh lists"));

    let settings_panel = Node::panel("controls-panel", Layout::Column).size(Size::Fill, Size::Fill).gap(6.0)
        .child(Node::title("c-title", "").size(Size::Fill, Size::Auto))
        .child(Node::container("controls", Layout::Column).size(Size::Fill, Size::Fill).gap(4.0))
        .child(Node::text("c-help", ""))
        .child(Node::button("reset", "Restore defaults"));

    Node::container("root", Layout::Layer).padding(96.0).child(
        Node::panel("panel", Layout::Column).size(Size::Fill, Size::Fill).gap(12.0)
            .child(Node::title("title", "Mods").size(Size::Fill, Size::Auto))
            .child(Node::tabs("tabs", &["Mods", "Settings", "Browse"]).size(Size::Fill, Size::Fill)
                .child(Node::container("tab-mods", Layout::Row).gap(24.0)
                    .child(Node::list("mods-list", LIST_ROWS).size(Size::Fixed(620.0), Size::Fill))
                    .child(detail))
                .child(Node::container("tab-settings", Layout::Row).gap(24.0)
                    .child(Node::list("settings-list", LIST_ROWS).size(Size::Fixed(520.0), Size::Fill))
                    .child(settings_panel))
                .child(Node::container("tab-browse", Layout::Row).gap(24.0)
                    .child(Node::list("browse-list", LIST_ROWS).size(Size::Fixed(620.0), Size::Fill))
                    .child(bdetail)))
            .child(Node::text("progress-text", ""))
            .child(Node::bar("progress-bar", 0.0, theme::ACCENT))
            .child(Node::text("status", ""))
            .child(Node::key_guide("guide", &[("KG_OK", "Select"), ("KG_Cancel", "Back"), ("KG_L1", "Previous tab"), ("KG_R1", "Next tab")])))
}

fn wrap(s: &str, width: usize, lines: usize) -> Vec<String> {
    let mut out: Vec<String> = Vec::new();
    let mut cur = String::new();
    for word in s.split_whitespace() {
        if !cur.is_empty() && cur.chars().count() + 1 + word.chars().count() > width {
            out.push(std::mem::take(&mut cur));
        }
        if !cur.is_empty() { cur.push(' '); }
        cur.push_str(word);
    }
    if !cur.is_empty() { out.push(cur); }
    if out.len() > lines {
        out.truncate(lines);
        if let Some(last) = out.last_mut() { last.push_str("..."); }
    }
    out
}

fn set_text(root: &mut Node, key: &str, text: &str, color: Option<bbmod::ui_layer::Color>) {
    if let Some(n) = root.find_mut(key) {
        n.visible = !text.is_empty();
        if let Kind::Text { text: t, color: c, .. } = &mut n.kind {
            if t != text { *t = text.to_owned(); }
            if let Some(col) = color { *c = col; }
        }
    }
}

fn set_rows(root: &mut Node, key: &str, rows: Vec<ListRow>, selected: Option<&str>) {
    let Some(n) = root.find_mut(key) else { return };
    let Kind::List(d) = &mut n.kind else { return };
    if d.rows() != rows.as_slice() {
        let rev = d.revision() + 1;
        let _ = d.set_rows(rev, rows);
    }
    if let Some(s) = selected { d.select(s); }
}

#[derive(Default)]
pub struct View { controls_sig: String }

/// Update the Browse tab of `root` from `browse`; `mods` says what is installed.
pub fn sync_browse(root: &mut Node, browse: &Browse, model_status: &str, mods: &[ModInfo]) {
    // Install progress, and the Browse tab's own messages when the menu has none.
    let progress = browse.progress_view();
    set_text(root, "progress-text", progress.as_ref().map_or("", |p| p.0.as_str()), Some(theme::BRIGHT));
    if let Some(b) = root.find_mut("progress-bar") {
        b.visible = progress.is_some();
        if let Kind::Bar { value, .. } = &mut b.kind { *value = progress.as_ref().map_or(0.0, |p| p.1); }
    }
    if model_status.is_empty() { set_text(root, "status", &browse.status, Some(theme::BRIGHT)); }
    let rows: Vec<ListRow> = browse.catalog.iter().map(|m| ListRow {
        key: m.entry.id.clone(), label: format!("{} {}", pad_mark(browse.mark(m, mods)), m.entry.name) }).collect();
    set_rows(root, "browse-list", rows, browse.selected.as_deref());
    match browse.selected_mod() {
        Some(m) => {
            set_text(root, "b-name", &m.entry.name, None);
            let (version, abi) = match &m.manifest { ManifestState::Loaded(mf) => (format!("   v{}.{}.{}", mf.version.0, mf.version.1, mf.version.2), Some(mf.abi)), _ => (String::new(), None) };
            let author = if m.entry.author.is_empty() { String::new() } else { format!("   by {}", m.entry.author) };
            set_text(root, "b-meta", &format!("{}{}{}", m.entry.id, version, author), Some(theme::SECONDARY));
            let (compat_line, color) = match &m.manifest {
                ManifestState::Unknown | ManifestState::Loading => ("Checking this mod...".to_owned(), theme::SECONDARY),
                ManifestState::Failed(e) => (format!("Cannot read this mod: {e}"), theme::ERROR),
                ManifestState::Loaded(_) => {
                    let c = compat(abi.unwrap_or(HOST_ABI), HOST_ABI);
                    (compat_text(c, abi.unwrap_or(HOST_ABI)), if c == crate::catalog::Compat::Compatible { theme::BRIGHT } else { theme::ERROR })
                }
            };
            set_text(root, "b-compat", &compat_line, Some(color));
            let desc_src = match &m.manifest { ManifestState::Loaded(mf) if !mf.description.is_empty() => mf.description.as_str(), _ => m.entry.description.as_str() };
            let desc = wrap(desc_src, 62, DESC_LINES);
            for i in 0..DESC_LINES { set_text(root, &format!("b-desc-{i}"), desc.get(i).map_or("", |s| s.as_str()), None); }
            set_text(root, "b-source", &format!("From {}   (add lists under Settings > Mod Manager)", m.source), Some(theme::SECONDARY));
        }
        None => { for k in ["b-name", "b-meta", "b-compat", "b-source"] { set_text(root, k, "", None); } for i in 0..DESC_LINES { set_text(root, &format!("b-desc-{i}"), "", None); } }
    }
    let busy = browse.busy();
    if let Some(b) = root.find_mut("b-install") {
        b.enabled = browse.can_install(mods);
        b.kind = Kind::Button { label: browse.install_label(mods).into() };
    }
    if let Some(b) = root.find_mut("b-remove") { b.enabled = browse.can_remove(mods); }
    if let Some(b) = root.find_mut("b-refresh") { b.enabled = !busy; }
    if let Some(b) = root.find_mut("b-remove-source") { b.enabled = browse.selected_mod().is_some(); }
}

fn pad_mark(m: &str) -> String { format!("{m:<11}") }

impl View {
    /// Update `root` to show `model`. `focus` is the focused widget key (for contextual help).
    pub fn sync(&mut self, root: &mut Node, model: &Model, focus: Option<&str>) {
        let rows: Vec<ListRow> = model.mods.iter().map(|m| ListRow {
            key: m.id.clone(), label: format!("{} {}   v{}.{}.{}", state_mark(m.state), m.name, m.version.0, m.version.1, m.version.2) }).collect();
        set_rows(root, "mods-list", rows, model.selected_mod.as_deref());
        let srows: Vec<ListRow> = model.mods_with_settings().map(|m| ListRow {
            key: m.id.clone(), label: format!("{}{}", m.name, if m.state == ModState::Active { "" } else { "   (not loaded)" }) }).collect();
        set_rows(root, "settings-list", srows, model.settings_mod.as_deref());

        match model.selected() {
            Some(m) => {
                set_text(root, "d-name", &m.name, None);
                let author = if m.author.is_empty() { String::new() } else { format!("   by {}", m.author) };
                set_text(root, "d-meta", &format!("{}   v{}.{}.{}{}", m.id, m.version.0, m.version.1, m.version.2, author), Some(theme::SECONDARY));
                let color = match m.state { ModState::Active => theme::BRIGHT, ModState::Faulted => theme::ERROR, _ => theme::PRIMARY };
                set_text(root, "d-state", &format!("Status: {}", if m.is_self { "Loaded (always on)" } else { state_text(m.state) }), Some(color));
                let desc = wrap(&m.description, 62, DESC_LINES);
                for i in 0..DESC_LINES { set_text(root, &format!("d-desc-{i}"), desc.get(i).map_or("", |s| s.as_str()), None); }
                let mut order = String::new();
                if !m.after.is_empty() { order.push_str(&format!("Loads after: {}", m.after)); }
                if !m.conflicts.is_empty() { if !order.is_empty() { order.push_str("   "); } order.push_str(&format!("Conflicts with: {}", m.conflicts)); }
                set_text(root, "d-order", &order, Some(theme::SECONDARY));
                set_text(root, "d-error", &if m.last_error.is_empty() { String::new() } else { format!("Last error: {}", m.last_error) }, Some(theme::ERROR));
                if let Some(t) = root.find_mut("toggle") {
                    t.enabled = !m.is_self && m.has_code && m.state != ModState::Disabled;
                    t.kind = Kind::Button { label: if m.is_self { "Core (cannot be unloaded)".into() }
                        else if m.state == ModState::Active { "Unload".into() } else { "Load".into() } };
                }
                if let Some(r) = root.find_mut("reload") { r.enabled = m.state == ModState::Active && !m.is_self && m.has_code; }
                if let Some(u) = root.find_mut("uninstall") { u.enabled = !m.is_self; }
            }
            None => { for k in ["d-name", "d-meta", "d-state", "d-order", "d-error"] { set_text(root, k, "", None); } }
        }

        set_text(root, "status", &model.status, Some(theme::BRIGHT));
        set_text(root, "c-title", &model.settings_mod.as_ref()
            .and_then(|id| model.mods.iter().find(|m| &m.id == id)).map_or(String::new(), |m| format!("{} settings", m.name)), None);
        let help = focus.and_then(|f| f.strip_prefix(CONTROL))
            .and_then(|k| model.entries.iter().find(|e| e.def.key == k)).map_or("", |e| e.def.help.as_str());
        set_text(root, "c-help", help, Some(theme::SECONDARY));

        let sig = format!("{:?}|{:?}", model.settings_mod, model.entries.iter().map(|e| (&e.def.key, &e.value, e.inactive, model.visible(&e.def))).collect::<Vec<_>>());
        if sig != self.controls_sig {
            self.controls_sig = sig;
            let children = controls(model);
            if let Some(c) = root.find_mut("controls") { c.children = children; }
            if let Some(r) = root.find_mut("reset") { r.visible = !model.entries.is_empty(); }
        }
    }
}

fn controls(model: &Model) -> Vec<Node> {
    let mut out = Vec::new();
    if model.entries.is_empty() {
        let msg = if model.settings_mod.is_some() { "This mod has not declared any settings." } else { "No mod has settings yet." };
        out.push(Node::text("c-empty", msg));
        return out;
    }
    let mut group = String::new();
    for e in model.entries.iter().filter(|e| model.visible(&e.def)) {
        if e.def.group != group && !e.def.group.is_empty() {
            group = e.def.group.clone();
            let mut g = Node::text(&format!("g:{group}"), &group);
            if let Kind::Text { color, bold, .. } = &mut g.kind { *color = theme::ACCENT; *bold = true; }
            out.push(g);
        }
        let key = format!("{CONTROL}{}", e.def.key);
        let mut label = e.def.label.clone();
        if e.def.restart { label.push_str(" (restart)"); }
        let mut node = match (&e.def.kind, &e.value) {
            (SKind::Bool { .. }, Value::Bool(v)) => Node::toggle(&key, &label, *v),
            (SKind::Int { min, max, step, .. }, Value::Int(v)) => Node::slider(&key, &label, *v as f32, *min as f32, *max as f32, *step as f32),
            (SKind::Float { min, max, step, .. }, Value::Float(v)) => Node::slider(&key, &label, *v as f32, *min as f32, *max as f32, *step as f32),
            (SKind::Enum { choices, .. }, Value::Enum(i)) => {
                let c: Vec<&str> = choices.iter().map(String::as_str).collect();
                Node::choice(&key, &label, &c, *i)
            }
            (SKind::Text { .. }, Value::Text(t)) => Node::button(&key, &format!("{label}: {}", if t.is_empty() { "(empty)" } else { t })),
            (SKind::Keybind { .. }, v @ Value::Keybind(k)) => {
                let opts = key_options(v);
                let names: Vec<&str> = opts.iter().map(|(_, n)| n.as_str()).collect();
                Node::choice(&key, &label, &names, opts.iter().position(|(c, _)| c == k).unwrap_or(0))
            }
            _ => continue,
        };
        node.enabled = !e.inactive;
        out.push(node.size(Size::Fill, Size::Auto));
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::info;
    use bbmod::settings::{Def, Entry};
    use bbmod::ui_layer::{encode, Canvas, DrawCmd, Estimated, LayerKind, Screen};

    fn model() -> Model {
        let mut m = Model::default();
        let mut menu = info("bbmod.manager", "Mod Manager", ModState::Active, true);
        menu.is_self = true;
        menu.description = "Lists mods and edits their settings. ".repeat(6);
        let mut a = info("a.core", "Core", ModState::Unloaded, true);
        a.last_error = "boom".into();
        m.set_mods(vec![menu, a]);
        m
    }

    #[test]
    fn lists_detail_and_buttons_follow_the_model() {
        let mut v = View::default();
        let mut s = Screen::new(build(), true);
        let m = model();
        s.update(|r| v.sync(r, &m, None));
        match &s.root.find("mods-list").unwrap().kind {
            Kind::List(d) => assert_eq!(d.rows().iter().map(|r| r.label.as_str()).collect::<Vec<_>>(),
                ["[On]  Mod Manager   v1.2.3", "[Off] Core   v1.2.3"]),
            _ => panic!(),
        }
        fn text(s: &Screen, k: &str) -> String { match &s.root.find(k).unwrap().kind { Kind::Text { text, .. } => text.clone(), _ => panic!() } }
        assert_eq!(text(&s, "d-name"), "Mod Manager");
        assert!(text(&s, "d-state").contains("always on"));
        assert!(s.root.find("d-desc-0").unwrap().visible && !s.root.find("d-desc-5").unwrap().visible);
        assert!(!s.root.find("toggle").unwrap().enabled && !s.root.find("reload").unwrap().enabled);
        let mut m = m;
        m.selected_mod = Some("a.core".into());
        s.update(|r| v.sync(r, &m, None));
        assert!(s.root.find("toggle").unwrap().enabled);
        assert!(matches!(&s.root.find("toggle").unwrap().kind, Kind::Button { label } if label == "Load"));
        assert!(text(&s, "d-error").contains("boom"));
        assert!(!s.root.find("reload").unwrap().enabled);
    }

    #[test]
    fn settings_controls_are_generated_from_the_schema_and_respect_dependencies() {
        let mut v = View::default();
        let mut s = Screen::new(build(), true);
        let mut m = model();
        m.settings_mod = Some("a.core".into());
        let e = |d: Def| { let value = d.default_value(); Entry { def: d, value, inactive: false } };
        m.entries = vec![
            e(Def::bool("on", "Enable", false).group("General").help("Turns it on.")),
            e(Def::int("lvl", "Level", 0, 10, 3).depends_on("on", 1).group("General")),
            e(Def::choice("mode", "Mode", &["A", "B"], 1).restart(true)),
            e(Def::text("name", "Name", 8, "")), e(Def::keybind("k", "Key", 61)),
        ];
        s.update(|r| v.sync(r, &m, Some("s:on")));
        let keys = |s: &Screen| s.root.find("controls").unwrap().children.iter().map(|n| n.key.clone()).collect::<Vec<_>>();
        assert_eq!(keys(&s), ["g:General", "s:on", "s:mode", "s:name", "s:k"]); // lvl hidden until `on`
        m.on_event_for_test("s:on");
        s.update(|r| v.sync(r, &m, Some("s:on")));
        assert_eq!(keys(&s), ["g:General", "s:on", "s:lvl", "s:mode", "s:name", "s:k"]);
        match &s.root.find("c-help").unwrap().kind { Kind::Text { text, .. } => assert_eq!(text, "Turns it on."), _ => panic!() }
        assert!(matches!(&s.root.find("s:mode").unwrap().kind, Kind::Choice { label, index: 1, .. } if label == "Mode (restart)"));
        assert!(matches!(&s.root.find("s:name").unwrap().kind, Kind::Button { label } if label == "Name: (empty)"));
    }

    #[test]
    fn the_menu_draws_and_encodes_for_the_host() {
        let mut v = View::default();
        let mut s = Screen::new(build(), true);
        s.update(|r| v.sync(r, &model(), None));
        let canvas = Canvas::fit((2560.0, 1440.0), (1920.0, 1080.0)).unwrap();
        let cmds = s.draw(LayerKind::Screen.bounds(), true, 1.0, &Estimated);
        assert!(cmds.iter().any(|c| matches!(c, DrawCmd::Text { text, .. } if text == "Mods")));
        assert!(cmds.iter().any(|c| matches!(c, DrawCmd::Image { name, .. } if name == "KG_OK")));
        let bytes = encode(&cmds);
        assert!(bytes.len() < bbmod::ui_layer::MAX_BYTES);
        assert_eq!(bbmod::ui_layer::decode(&bytes).unwrap().len(), cmds.len());
        assert!(canvas.scale > 1.3);
    }

    impl Model {
        fn on_event_for_test(&mut self, key: &str) {
            if let Some(e) = self.entries.iter_mut().find(|e| e.def.key == key.trim_start_matches(CONTROL)) { e.value = Value::Bool(true); }
        }
    }
}
