//! Mod-owned list/detail data for the forthcoming native browser experiment.
//! These tests establish state semantics, not rendered native UI or input routing.
use bbmod::ui_state::{ListState, SnapshotError};
use std::num::NonZeroUsize;

struct Entry { key: &'static str, title: &'static str, details: &'static str }
const EQUIPMENT: &[Entry] = &[
    Entry { key: "lantern", title: "Survey lantern", details: "A sample tool for lighting forgotten paths." },
    Entry { key: "compass", title: "Brass compass", details: "A sample instrument whose needle points home." },
    Entry { key: "journal", title: "Field journal", details: "A sample collection of handwritten observations." },
    Entry { key: "lens", title: "Clouded lens", details: "A sample lens for examining small markings." },
    Entry { key: "bell", title: "Signal bell", details: "A sample bell carried by the survey party." },
    Entry { key: "rope", title: "Coiled rope", details: "A sample length of rope for steep descents." },
    Entry { key: "case", title: "Specimen case", details: "A sample box for keeping fragile discoveries." },
    Entry { key: "map", title: "Weathered map", details: "A sample chart with an unfinished border." },
    Entry { key: "chalk", title: "Marking chalk", details: "A sample tool for marking a safe return." },
];
const NOTES: &[Entry] = &[
    Entry { key: "arrival", title: "Arrival", details: "The survey party reached the old gate at dusk." },
    Entry { key: "route", title: "Lower route", details: "The lower passage remains unexplored." },
];

#[test]
fn categories_restore_independent_focus_and_details_follow_keys() {
    let rows = NonZeroUsize::new(7).unwrap();
    let mut equipment = ListState::new(rows);
    let mut notes = ListState::new(rows);
    equipment.replace(1, EQUIPMENT.iter().map(|e| e.key).collect()).unwrap();
    notes.replace(1, NOTES.iter().map(|e| e.key).collect()).unwrap();
    for _ in 0..8 { equipment.navigate(true); }
    notes.navigate(true); // A separate category never changes the equipment view.
    assert_eq!((equipment.selected_key(), equipment.visible_range()), (Some(&"chalk"), 2..9));
    assert_eq!(notes.selected_key(), Some(&"route"));
    let selected = EQUIPMENT.iter().find(|e| Some(&e.key) == equipment.selected_key()).unwrap();
    assert_eq!(selected.title, "Marking chalk");
    assert!(selected.details.contains("safe return"));
    equipment.replace(2, EQUIPMENT.iter().rev().map(|e| e.key).collect()).unwrap();
    assert_eq!((equipment.selected_key(), equipment.visible_range()), (Some(&"chalk"), 0..7));
    assert_eq!(equipment.select(1, &"lantern"), Err(SnapshotError::StaleRevision));
    notes.replace(2, vec![]).unwrap();
    assert_eq!(notes.selected_key(), None);
    assert_eq!(equipment.selected_key(), Some(&"chalk"));
}
