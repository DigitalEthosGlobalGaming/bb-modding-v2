//! Read-only list/detail browser over mod-owned sample data.
//! The model is renderer-independent; `nodes` projects it onto verified native
//! menu rows (not a full-screen layout). No game data is read or written.
use bbmod::native_menu::{Kind, Node};
use bbmod::ui_state::ListState;
use std::num::NonZeroUsize;

pub const PARENT: &str = "example.ui-examples/browser";
const ENTRY_PREFIX: &str = "browser-entry-";
const CATEGORY_KEY: &str = "browser-category";

pub struct Entry { pub key: &'static str, pub title: &'static str, pub details: &'static str }
pub struct Category { pub name: &'static str, pub entries: &'static [Entry] }

pub const CATEGORIES: &[Category] = &[
    Category { name: "Equipment", entries: &[
        Entry { key: "lantern", title: "Survey lantern", details: "A sample tool for lighting forgotten paths." },
        Entry { key: "compass", title: "Brass compass", details: "A sample instrument whose needle points home." },
        Entry { key: "journal", title: "Field journal", details: "A sample collection of handwritten observations." },
        Entry { key: "lens", title: "Clouded lens", details: "A sample lens for examining small markings." },
        Entry { key: "bell", title: "Signal bell", details: "A sample bell carried by the survey party." },
        Entry { key: "rope", title: "Coiled rope", details: "A sample length of rope for steep descents." },
        Entry { key: "case", title: "Specimen case", details: "A sample box for keeping fragile discoveries." },
        Entry { key: "map", title: "Weathered map", details: "A sample chart with an unfinished border." },
        Entry { key: "chalk", title: "Marking chalk", details: "A sample tool for marking a safe return." },
    ] },
    Category { name: "Notes", entries: &[
        Entry { key: "arrival", title: "Arrival", details: "The survey party reached the old gate at dusk." },
        Entry { key: "route", title: "Lower route", details: "The lower passage remains unexplored." },
    ] },
];

/// One `ListState` per category retains each category's selection and viewport.
pub struct Browser {
    category: usize,
    lists: Vec<ListState<&'static str>>,
}

impl Browser {
    pub fn new() -> Self {
        let rows = NonZeroUsize::new(7).unwrap();
        let lists = CATEGORIES.iter().map(|c| {
            let mut list = ListState::new(rows);
            list.replace(1, c.entries.iter().map(|e| e.key).collect()).expect("unique sample keys");
            list
        }).collect();
        Self { category: 0, lists }
    }
    pub fn category(&self) -> usize { self.category }
    pub fn selected(&self) -> Option<&'static Entry> {
        let key = self.lists[self.category].selected_key()?;
        CATEGORIES[self.category].entries.iter().find(|e| e.key == *key)
    }
    /// Stand-in for L1/R1 until shoulder routing exists: cycle and wrap.
    pub fn next_category(&mut self) { self.category = (self.category + 1) % CATEGORIES.len(); }
    /// Returns true if the entry exists in the current category and became selected.
    pub fn activate(&mut self, key: &str) -> bool {
        let Some(key) = key.strip_prefix(ENTRY_PREFIX) else { return false };
        let list = &mut self.lists[self.category];
        let rev = list.revision();
        let Some(found) = CATEGORIES[self.category].entries.iter().find(|e| e.key == key) else { return false };
        list.select(rev, &found.key).unwrap_or(false)
    }
    pub fn is_category_key(key: &str) -> bool { key == CATEGORY_KEY }

    /// Screen node, a category switcher, a detail row, then the entries.
    pub fn nodes(&self, parent: &str) -> Vec<Node> {
        let cat = &CATEGORIES[self.category];
        let mut screen = Node::new(Kind::Screen, "browser", parent, "Sample browser");
        screen.help = "Read-only sample list and details owned by this example. Select an entry to show its details.".into();
        let mut switch = Node::new(Kind::Action, CATEGORY_KEY, PARENT,
            &format!("Category: {} ({}/{})", cat.name, self.category + 1, CATEGORIES.len()));
        switch.help = "Cycle categories. Each category remembers its own selection.".into();
        let mut detail = Node::new(Kind::Info, "browser-detail", PARENT, &match self.selected() {
            Some(e) => format!("{}: {}", e.title, e.details),
            None => "(Nothing selected)".to_owned(),
        });
        detail.label.truncate(clip(&detail.label, 127));
        let mut nodes = vec![screen, switch, detail];
        for e in cat.entries {
            let selected = self.selected().map(|s| s.key) == Some(e.key);
            let mut n = Node::new(Kind::Action, &format!("{ENTRY_PREFIX}{}", e.key), PARENT,
                &format!("{}{}", if selected { "> " } else { "" }, e.title));
            n.help = e.details.into();
            nodes.push(n);
        }
        for (i, n) in nodes.iter_mut().enumerate() { n.order = i as i32; }
        nodes
    }
}

fn clip(s: &str, max: usize) -> usize {
    if s.len() <= max { return s.len(); }
    let mut end = max;
    while !s.is_char_boundary(end) { end -= 1; }
    end
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn details_follow_activation_and_categories_keep_selection() {
        let mut b = Browser::new();
        assert_eq!(b.selected().unwrap().key, "lantern");
        assert!(b.activate("browser-entry-chalk"));
        assert!(!b.activate("browser-entry-chalk"), "unchanged selection");
        assert!(!b.activate("browser-entry-route"), "other category's key");
        assert!(!b.activate("overview"), "foreign key");
        b.next_category();
        assert_eq!(b.selected().unwrap().key, "arrival");
        assert!(b.activate("browser-entry-route"));
        b.next_category();
        assert_eq!(b.selected().unwrap().key, "chalk");
    }
    #[test]
    fn nodes_have_unique_keys_and_fit_native_limits() {
        let mut b = Browser::new();
        for _ in 0..CATEGORIES.len() {
            let nodes = b.nodes("game.mods");
            let mut keys: Vec<_> = nodes.iter().map(|n| n.key.clone()).collect();
            keys.sort(); keys.dedup();
            assert_eq!(keys.len(), nodes.len());
            assert_eq!(nodes.iter().filter(|n| n.label.starts_with("> ")).count(), 1);
            assert!(nodes.iter().all(|n| n.label.len() <= 127 && n.help.len() <= 255));
            b.next_category();
        }
    }
}
