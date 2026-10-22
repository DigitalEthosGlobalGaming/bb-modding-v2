//! Renderer-independent state for composed UI lists.
//!
//! This module makes no host calls and grants no input capture. A native Screen,
//! list/detail renderer and shoulder-button routing are separate capabilities.
//! Keys belong to the contributing mod; they are not engine item IDs or row
//! indices. Keep one state per tab to retain each tab's selection and viewport.

use std::{collections::HashSet, hash::Hash, num::NonZeroUsize, ops::Range};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SnapshotError {
    StaleRevision,
    DuplicateKey,
}

/// A single-select list and its bounded visible range. Row payloads and details
/// remain in the caller's snapshot. No native object retains this state; an
/// eventual renderer must copy keys/payloads into host-owned storage. There is
/// no dependency on any gameplay data type.
#[derive(Clone, Debug)]
pub struct ListState<K> {
    revision: u64,
    keys: Vec<K>,
    selected: Option<usize>,
    first: usize,
    visible_rows: NonZeroUsize,
}

impl<K: Clone + Eq + Hash> ListState<K> {
    pub fn new(visible_rows: NonZeroUsize) -> Self {
        Self { revision: 0, keys: Vec::new(), selected: None, first: 0, visible_rows }
    }

    pub fn revision(&self) -> u64 { self.revision }
    pub fn keys(&self) -> &[K] { &self.keys }
    pub fn selected_index(&self) -> Option<usize> { self.selected }
    pub fn selected_key(&self) -> Option<&K> { self.selected.map(|i| &self.keys[i]) }
    pub fn visible_range(&self) -> Range<usize> {
        self.first..self.first.saturating_add(self.visible_rows.get()).min(self.keys.len())
    }

    /// Atomically accept a newer snapshot with unique keys. Preserve the
    /// selected key across reordering. If removed, select the row at its former
    /// position, clamped to the final row. Empty lists have no selection.
    /// Rejected snapshots leave revision, selection and viewport unchanged.
    pub fn replace(&mut self, revision: u64, keys: Vec<K>) -> Result<(), SnapshotError> {
        if revision <= self.revision { return Err(SnapshotError::StaleRevision); }
        let mut unique = HashSet::with_capacity(keys.len());
        if keys.iter().any(|key| !unique.insert(key)) { return Err(SnapshotError::DuplicateKey); }
        let selected = self.selected_key().and_then(|key| keys.iter().position(|k| k == key))
            .or_else(|| (!keys.is_empty()).then(|| self.selected.unwrap_or(0).min(keys.len() - 1)));
        self.keys = keys;
        self.selected = selected;
        self.revision = revision;
        self.reveal();
        Ok(())
    }

    /// Apply a row event only to the snapshot that produced it. Unknown keys
    /// return false; neither unknown nor stale events move selection.
    pub fn select(&mut self, revision: u64, key: &K) -> Result<bool, SnapshotError> {
        if revision != self.revision { return Err(SnapshotError::StaleRevision); }
        let Some(index) = self.keys.iter().position(|k| k == key) else { return Ok(false) };
        let changed = self.selected != Some(index);
        self.selected = Some(index);
        self.reveal();
        Ok(changed)
    }

    /// Move one row, stopping at the ends. Physical input bindings, repeat and
    /// foreground ownership belong to the input adapter.
    pub fn navigate(&mut self, forward: bool) -> bool {
        let Some(index) = self.selected else { return false };
        let next = if forward { index.saturating_add(1).min(self.keys.len() - 1) }
                   else { index.saturating_sub(1) };
        self.selected = Some(next);
        self.reveal();
        next != index
    }

    pub fn set_visible_rows(&mut self, rows: NonZeroUsize) {
        self.visible_rows = rows;
        self.reveal();
    }

    fn reveal(&mut self) {
        let Some(index) = self.selected else { self.first = 0; return };
        let rows = self.visible_rows.get();
        self.first = self.first.min(self.keys.len().saturating_sub(rows));
        if index < self.first { self.first = index; }
        else if index - self.first >= rows { self.first = index + 1 - rows; }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn list() -> ListState<u32> { ListState::new(NonZeroUsize::new(3).unwrap()) }

    #[test]
    fn stable_selection_follows_reordering_and_has_nearby_removal_fallback() {
        let mut s = list();
        s.replace(1, vec![10, 20, 30, 40, 50]).unwrap();
        s.select(1, &40).unwrap();
        assert_eq!(s.visible_range(), 1..4);
        s.replace(2, vec![40, 10, 20, 30, 50]).unwrap();
        assert_eq!(s.selected_key(), Some(&40));
        assert_eq!(s.visible_range(), 0..3);
        s.select(2, &50).unwrap();
        s.replace(3, vec![10, 20]).unwrap();
        assert_eq!(s.selected_key(), Some(&20));
        assert_eq!(s.visible_range(), 0..2);
    }

    #[test]
    fn rejected_updates_and_old_events_are_atomic() {
        let mut s = list();
        s.replace(4, vec![1, 2, 3, 4]).unwrap();
        s.select(4, &4).unwrap();
        assert_eq!(s.replace(5, vec![1, 1]), Err(SnapshotError::DuplicateKey));
        assert_eq!(s.replace(4, vec![9]), Err(SnapshotError::StaleRevision));
        assert_eq!(s.select(3, &1), Err(SnapshotError::StaleRevision));
        assert_eq!(s.select(4, &9), Ok(false));
        assert_eq!((s.revision(), s.selected_key(), s.visible_range()), (4, Some(&4), 1..4));
        assert_eq!(s.keys(), &[1, 2, 3, 4]);
    }

    #[test]
    fn empty_navigation_and_window_resize_stay_in_bounds() {
        let mut s = list();
        assert!(!s.navigate(true));
        assert_eq!(s.visible_range(), 0..0);
        s.replace(1, (0..10000).collect()).unwrap();
        s.select(1, &9999).unwrap();
        assert!(!s.navigate(true));
        assert_eq!(s.visible_range(), 9997..10000);
        s.set_visible_rows(NonZeroUsize::new(7).unwrap());
        assert_eq!(s.visible_range(), 9993..10000);
        assert!(s.navigate(false));
        s.replace(2, vec![]).unwrap();
        assert_eq!((s.selected_key(), s.visible_range()), (None, 0..0));
        s.replace(3, vec![7]).unwrap();
        assert_eq!(s.selected_key(), Some(&7));
        assert!(!s.navigate(false));
    }
}
