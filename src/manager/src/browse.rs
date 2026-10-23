//! State and behaviour of the Browse tab: mod list sources, the catalog they provide, and
//! installing, updating and removing mods. Pure: events and job results in, [`Out`] actions out;
//! `lib.rs` executes the actions against the host and the worker thread.

use crate::catalog::{self, compat, Compat, ListEntry, Manifest, ModList, Source, HOST_ABI, OFFICIAL_SOURCE};
use crate::jobs::{Done, Job};
use bbmod::settings::{ModInfo, ModState};
use bbmod::ui_layer::EventKind;

pub const TAB_BROWSE: usize = 2;
/// Ticks (about a third of a second apart while open) to wait for a mod to unload or load.
const WAIT_TICKS: u32 = 90;

#[derive(Clone, Debug, PartialEq)]
pub enum ManifestState { Unknown, Loading, Loaded(Manifest), Failed(String) }

#[derive(Clone, Debug, PartialEq)]
pub struct CatalogMod { pub entry: ListEntry, pub source: String, pub manifest: ManifestState }

#[derive(Debug)]
pub enum Out {
    Job(Job),
    Unload(String),
    Load(String),
    SaveSources(Vec<String>),
    /// Ask for a source in the text dialog; the result comes back through [`Browse::add_source`].
    AddSourceDialog,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Task { Install, Remove }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Stage { WaitUnload, Working }
struct Pending { id: String, task: Task, stage: Stage, waited: u32 }

#[derive(Default)]
pub struct Browse {
    pub sources: Vec<String>,
    pub catalog: Vec<CatalogMod>,
    pub selected: Option<String>,
    pub status: String,
    /// Sources whose list could not be read, with the reason.
    pub failed: Vec<(String, String)>,
    lists: Vec<(String, ModList)>,
    outstanding: usize,
    lists_out: usize,
    pending: Vec<Pending>,
    to_load: Vec<(String, u32)>,
}

pub fn installed<'a>(mods: &'a [ModInfo], id: &str) -> Option<&'a ModInfo> { mods.iter().find(|m| m.id == id) }

impl Browse {
    pub fn new(mut sources: Vec<String>) -> Browse {
        if sources.is_empty() { sources.push(OFFICIAL_SOURCE.to_owned()); }
        Browse { sources, ..Default::default() }
    }

    pub fn busy(&self) -> bool { self.outstanding > 0 || !self.pending.is_empty() }

    /// Work is in flight or a freshly installed mod still has to be loaded: keep polling.
    pub fn active(&self) -> bool { self.busy() || !self.to_load.is_empty() }

    pub fn selected_mod(&self) -> Option<&CatalogMod> {
        let id = self.selected.as_deref()?;
        self.catalog.iter().find(|m| m.entry.id == id)
    }

    /// Fetch every source again.
    pub fn refresh(&mut self) -> Vec<Out> {
        self.failed.clear();
        self.status = "Loading mod lists...".into();
        self.outstanding += self.sources.len();
        self.lists_out = self.sources.len();
        self.sources.iter().map(|s| Out::Job(Job::FetchList { source: s.clone() })).collect()
    }

    pub fn add_source(&mut self, text: &str) -> Vec<Out> {
        match Source::parse(text) {
            Err(e) => { self.status = e; vec![] }
            Ok(src) => {
                let key = src.key();
                if self.sources.iter().any(|s| s.eq_ignore_ascii_case(&key)) { self.status = "That source is already added.".into(); return vec![]; }
                self.sources.push(key.clone());
                self.status = format!("Added {key}. Loading...");
                self.outstanding += 1;
                self.lists_out += 1;
                vec![Out::SaveSources(self.sources.clone()), Out::Job(Job::FetchList { source: key })]
            }
        }
    }

    fn remove_source(&mut self) -> Vec<Out> {
        let Some(m) = self.selected_mod() else { self.status = "Select a mod to remove its source.".into(); return vec![] };
        let src = m.source.clone();
        if src.eq_ignore_ascii_case(OFFICIAL_SOURCE) { self.status = "The official list cannot be removed.".into(); return vec![]; }
        self.sources.retain(|s| s != &src);
        self.lists.retain(|(s, _)| s != &src);
        self.rebuild();
        self.status = format!("Removed source {src}. Installed mods stay installed.");
        vec![Out::SaveSources(self.sources.clone())]
    }

    /// Rebuild the catalog from the loaded lists, keeping manifests already fetched.
    fn rebuild(&mut self) {
        let old = std::mem::take(&mut self.catalog);
        let mut seen = std::collections::HashSet::new();
        for src in &self.sources {
            let Some((_, list)) = self.lists.iter().find(|(s, _)| s == src) else { continue };
            for e in &list.mods {
                if !seen.insert(e.id.clone()) { continue; } // an earlier source wins
                let manifest = old.iter().find(|o| o.entry == *e).map_or(ManifestState::Unknown, |o| o.manifest.clone());
                self.catalog.push(CatalogMod { entry: e.clone(), source: src.clone(), manifest });
            }
        }
        if self.selected_mod().is_none() { self.selected = self.catalog.first().map(|m| m.entry.id.clone()); }
    }

    /// Select a mod and start reading its manifest if needed.
    fn select(&mut self, id: &str) -> Vec<Out> {
        self.selected = Some(id.to_owned());
        self.status.clear();
        self.fetch_manifest(id)
    }

    fn fetch_manifest(&mut self, id: &str) -> Vec<Out> {
        let Some(m) = self.catalog.iter_mut().find(|m| m.entry.id == id) else { return vec![] };
        if m.manifest != ManifestState::Unknown { return vec![]; }
        m.manifest = ManifestState::Loading;
        self.outstanding += 1;
        vec![Out::Job(Job::FetchManifest { entry: m.entry.clone() })]
    }

    /// Fetch the manifest of the selected mod once the catalog changes under the selection.
    fn ensure_selected_manifest(&mut self) -> Vec<Out> {
        match self.selected.clone() { Some(id) => self.fetch_manifest(&id), None => vec![] }
    }

    pub fn can_install(&self, mods: &[ModInfo]) -> bool {
        let Some(m) = self.selected_mod() else { return false };
        matches!(&m.manifest, ManifestState::Loaded(mf) if compat(mf.abi, HOST_ABI) == Compat::Compatible)
            && !self.pending.iter().any(|p| p.id == m.entry.id)
            && !installed(mods, &m.entry.id).is_some_and(|i| i.is_self)
    }

    pub fn can_remove(&self, mods: &[ModInfo]) -> bool {
        self.selected_mod().is_some_and(|m| installed(mods, &m.entry.id).is_some_and(|i| !i.is_self) && !self.pending.iter().any(|p| p.id == m.entry.id))
    }

    /// "Install", "Update" or "Reinstall" for the selected mod.
    pub fn install_label(&self, mods: &[ModInfo]) -> &'static str {
        let Some(m) = self.selected_mod() else { return "Install" };
        match (installed(mods, &m.entry.id), &m.manifest) {
            (None, _) => "Install",
            (Some(i), ManifestState::Loaded(mf)) if mf.version > i.version => "Update",
            _ => "Reinstall",
        }
    }

    /// One-line state shown in the list.
    pub fn mark(&self, m: &CatalogMod, mods: &[ModInfo]) -> &'static str {
        match (installed(mods, &m.entry.id), &m.manifest) {
            (Some(i), ManifestState::Loaded(mf)) if mf.version > i.version => "[Update]",
            (Some(_), _) => "[Installed]",
            _ => "[New]",
        }
    }

    pub fn on_event(&mut self, key: &str, kind: EventKind, mods: &[ModInfo]) -> Vec<Out> {
        match (key, kind) {
            ("browse-list", EventKind::Selected(id)) => self.select(&id),
            ("browse-list", EventKind::RowActivated(_)) | ("b-install", EventKind::Activated) => self.install(mods),
            ("b-remove", EventKind::Activated) => self.remove(mods),
            ("b-add", EventKind::Activated) => vec![Out::AddSourceDialog],
            ("b-refresh", EventKind::Activated) => self.refresh(),
            ("b-remove-source", EventKind::Activated) => self.remove_source(),
            _ => vec![],
        }
    }

    fn install(&mut self, mods: &[ModInfo]) -> Vec<Out> {
        if !self.can_install(mods) {
            self.status = match self.selected_mod().map(|m| &m.manifest) {
                Some(ManifestState::Loading) => "Still checking this mod...".into(),
                Some(ManifestState::Loaded(mf)) => catalog::compat_text(compat(mf.abi, HOST_ABI), mf.abi),
                Some(ManifestState::Failed(e)) => e.clone(),
                _ => "Select a mod first.".into(),
            };
            return vec![];
        }
        let m = self.selected_mod().cloned().expect("checked");
        let name = m.entry.name.clone();
        match installed(mods, &m.entry.id) {
            Some(i) if i.state == ModState::Active => {
                self.status = format!("Unloading {name} before updating...");
                self.pending.push(Pending { id: m.entry.id.clone(), task: Task::Install, stage: Stage::WaitUnload, waited: 0 });
                vec![Out::Unload(m.entry.id)]
            }
            _ => {
                self.status = format!("Installing {name}...");
                self.pending.push(Pending { id: m.entry.id.clone(), task: Task::Install, stage: Stage::Working, waited: 0 });
                vec![Out::Job(Job::Install { entry: m.entry })]
            }
        }
    }

    fn remove(&mut self, mods: &[ModInfo]) -> Vec<Out> {
        if !self.can_remove(mods) { self.status = "That mod is not installed.".into(); return vec![]; }
        let id = self.selected.clone().expect("checked");
        self.status = format!("Removing {id}...");
        if installed(mods, &id).is_some_and(|i| i.state == ModState::Active) {
            self.pending.push(Pending { id: id.clone(), task: Task::Remove, stage: Stage::WaitUnload, waited: 0 });
            vec![Out::Unload(id)]
        } else {
            self.pending.push(Pending { id: id.clone(), task: Task::Remove, stage: Stage::Working, waited: 0 });
            vec![Out::Job(Job::Remove { id })]
        }
    }

    /// A worker result arrived.
    pub fn on_done(&mut self, done: Done, mods: &[ModInfo]) -> Vec<Out> {
        self.outstanding = self.outstanding.saturating_sub(1);
        match done {
            Done::List { source, result } => {
                self.lists_out = self.lists_out.saturating_sub(1);
                match result {
                    Ok(list) => { self.lists.retain(|(s, _)| s != &source); self.lists.push((source, list)); }
                    Err(e) => self.failed.push((source, e)),
                }
                self.rebuild();
                if self.lists_out == 0 { self.status = self.list_summary(); }
                self.ensure_selected_manifest()
            }
            Done::Manifest { id, result } => {
                if let Some(m) = self.catalog.iter_mut().find(|m| m.entry.id == id) {
                    m.manifest = match result { Ok(mf) => ManifestState::Loaded(mf), Err(e) => ManifestState::Failed(e) };
                }
                vec![]
            }
            Done::Installed { id, result } => {
                self.pending.retain(|p| !(p.id == id && p.task == Task::Install));
                match result {
                    Ok(done) => {
                        self.status = format!("Installed {id} {}.{}.{}.", done.version.0, done.version.1, done.version.2);
                        self.to_load.push((id, 0));
                    }
                    Err(e) => self.status = format!("Install failed: {e}"),
                }
                let _ = mods;
                vec![]
            }
            Done::Removed { id, result } => {
                self.pending.retain(|p| !(p.id == id && p.task == Task::Remove));
                self.status = match result { Ok(()) => format!("Removed {id}. A copy is kept in mod-data/bbmod.manager/previous."), Err(e) => format!("Remove failed: {e}") };
                vec![]
            }
        }
    }

    fn list_summary(&self) -> String {
        match (self.catalog.len(), self.failed.first()) {
            (0, Some((s, e))) => format!("Could not read {s}: {e}"),
            (n, Some((s, e))) => format!("{n} mods. Could not read {s}: {e}"),
            (0, None) => "These lists contain no mods.".into(),
            (n, None) => format!("{n} mods available."),
        }
    }

    /// Advance waits against a fresh mod snapshot (about three times a second while open).
    pub fn tick(&mut self, mods: &[ModInfo]) -> Vec<Out> {
        let mut out = Vec::new();
        for p in &mut self.pending {
            if p.stage != Stage::WaitUnload { continue; }
            p.waited += 1;
            let loaded = installed(mods, &p.id).is_some_and(|i| i.state == ModState::Active);
            if !loaded {
                p.stage = Stage::Working;
                out.push(match p.task {
                    Task::Install => match self.catalog.iter().find(|m| m.entry.id == p.id) {
                        Some(m) => Out::Job(Job::Install { entry: m.entry.clone() }),
                        None => continue,
                    },
                    Task::Remove => Out::Job(Job::Remove { id: p.id.clone() }),
                });
            }
        }
        let timed_out: Vec<String> = self.pending.iter().filter(|p| p.stage == Stage::WaitUnload && p.waited > WAIT_TICKS).map(|p| p.id.clone()).collect();
        for id in &timed_out { self.status = format!("{id} did not unload; nothing was changed."); }
        self.pending.retain(|p| !(p.stage == Stage::WaitUnload && p.waited > WAIT_TICKS));

        // After an install: a known but unloaded mod needs an explicit Load; a new folder is
        // found and loaded by the host itself.
        let mut keep = Vec::new();
        for (id, waited) in std::mem::take(&mut self.to_load) {
            match installed(mods, &id).map(|i| i.state) {
                Some(ModState::Active) | Some(ModState::Faulted) | Some(ModState::Disabled) => {}
                Some(ModState::Unloaded) => out.push(Out::Load(id)),
                _ if waited < WAIT_TICKS => keep.push((id, waited + 1)),
                _ => {}
            }
        }
        self.to_load = keep;
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::catalog::Manifest;
    use crate::install::Installed;
    use crate::model::info;

    fn entry(id: &str) -> ListEntry {
        serde_json::from_str(&format!(r#"{{"id":"{id}","name":"{id}","repo":"o/{id}"}}"#)).unwrap()
    }
    fn list(ids: &[&str]) -> ModList { ModList { name: "L".into(), description: String::new(), version: 1, mods: ids.iter().map(|i| entry(i)).collect() } }
    fn manifest(id: &str, v: (u16, u16, u16), abi: (u32, u32)) -> Manifest { Manifest { id: id.into(), name: id.into(), version: v, abi, ..Default::default() } }
    fn jobs(out: &[Out]) -> usize { out.iter().filter(|o| matches!(o, Out::Job(_))).count() }

    fn loaded() -> Browse {
        let mut b = Browse::new(vec!["a/one".into(), OFFICIAL_SOURCE.into()]);
        assert_eq!(jobs(&b.refresh()), 2);
        b.on_done(Done::List { source: "a/one".into(), result: Ok(list(&["x.mod", "y.mod"])) }, &[]);
        b.on_done(Done::List { source: OFFICIAL_SOURCE.into(), result: Ok(list(&["y.mod", "z.mod"])) }, &[]);
        b
    }

    #[test]
    fn lists_merge_with_earlier_sources_winning_and_failures_reported() {
        let mut b = loaded();
        assert_eq!(b.catalog.iter().map(|m| (m.entry.id.as_str(), m.source.as_str())).collect::<Vec<_>>(),
            [("x.mod", "a/one"), ("y.mod", "a/one"), ("z.mod", OFFICIAL_SOURCE)]);
        assert_eq!(b.status, "3 mods available.");
        b.refresh();
        b.on_done(Done::List { source: "a/one".into(), result: Err("offline".into()) }, &[]);
        b.on_done(Done::List { source: OFFICIAL_SOURCE.into(), result: Ok(list(&["z.mod"])) }, &[]);
        assert!(b.status.contains("a/one") && b.status.contains("offline"), "{}", b.status);
    }

    #[test]
    fn selecting_reads_the_manifest_once_and_gates_install_on_compatibility() {
        let mut b = loaded();
        assert!(b.selected.as_deref() == Some("x.mod") && b.selected_mod().unwrap().manifest == ManifestState::Loading);
        assert!(!b.can_install(&[]));
        b.on_done(Done::Manifest { id: "x.mod".into(), result: Ok(manifest("x.mod", (1, 0, 0), (1, 1))) }, &[]);
        assert!(b.can_install(&[]));
        assert!(b.on_event("browse-list", EventKind::Selected("x.mod".into()), &[]).is_empty()); // already known
        assert_eq!(jobs(&b.on_event("browse-list", EventKind::Selected("z.mod".into()), &[])), 1);
        b.on_done(Done::Manifest { id: "z.mod".into(), result: Ok(manifest("z.mod", (1, 0, 0), (1, 99))) }, &[]);
        assert!(!b.can_install(&[]));
        assert!(b.on_event("b-install", EventKind::Activated, &[]).is_empty() && b.status.contains("newer mod loader"), "{}", b.status);
    }

    #[test]
    fn installing_a_loaded_mod_unloads_it_first_then_installs_then_loads() {
        let mut b = loaded();
        b.on_done(Done::Manifest { id: "x.mod".into(), result: Ok(manifest("x.mod", (1, 2, 0), (1, 1))) }, &[]);
        let mut running = info("x.mod", "X", ModState::Active, false); // version 1.2.3 is installed
        running.version = (1, 0, 0);
        let mods = [running.clone()];
        assert_eq!(b.install_label(&mods), "Update");
        let out = b.on_event("b-install", EventKind::Activated, &mods);
        assert!(matches!(out.as_slice(), [Out::Unload(id)] if id == "x.mod") && b.busy());
        assert!(!b.can_install(&mods)); // already in progress
        assert!(b.tick(&mods).is_empty()); // still loaded
        let mut stopped = running.clone(); stopped.state = ModState::Unloaded;
        let out = b.tick(&[stopped.clone()]);
        assert!(matches!(out.as_slice(), [Out::Job(Job::Install { entry })] if entry.id == "x.mod"));
        assert!(b.tick(&[stopped.clone()]).is_empty());
        b.on_done(Done::Installed { id: "x.mod".into(), result: Ok(Installed { id: "x.mod".into(), version: (1, 2, 0), replaced: true }) }, &[stopped.clone()]);
        assert!(b.status.starts_with("Installed x.mod 1.2.0") && !b.busy());
        assert!(matches!(b.tick(&[stopped]).as_slice(), [Out::Load(id)] if id == "x.mod"));
        assert!(b.tick(&[running]).is_empty()); // loaded again: done
    }

    #[test]
    fn a_new_install_is_left_to_the_host_to_discover_and_failures_are_shown() {
        let mut b = loaded();
        b.on_done(Done::Manifest { id: "x.mod".into(), result: Ok(manifest("x.mod", (1, 0, 0), (1, 1))) }, &[]);
        let out = b.on_event("browse-list", EventKind::RowActivated("x.mod".into()), &[]);
        assert!(matches!(out.as_slice(), [Out::Job(Job::Install { .. })]));
        b.on_done(Done::Installed { id: "x.mod".into(), result: Err("no network".into()) }, &[]);
        assert!(b.status.contains("no network") && !b.busy());
        b.on_event("b-install", EventKind::Activated, &[]);
        b.on_done(Done::Installed { id: "x.mod".into(), result: Ok(Installed { id: "x.mod".into(), version: (1, 0, 0), replaced: false }) }, &[]);
        assert!(b.tick(&[]).is_empty()); // not listed yet: keep waiting without acting
        let discovered = info("x.mod", "X", ModState::Discovered, false);
        assert!(b.tick(&[discovered]).is_empty()); // the host loads discovered mods itself
    }

    #[test]
    fn removal_unloads_first_and_never_touches_the_manager() {
        let mut b = loaded();
        assert!(b.on_event("b-remove", EventKind::Activated, &[]).is_empty());
        let mut me = info("x.mod", "X", ModState::Active, false);
        me.is_self = true;
        assert!(!b.can_remove(&[me]));
        let on = info("x.mod", "X", ModState::Active, false);
        assert!(matches!(b.on_event("b-remove", EventKind::Activated, &[on.clone()]).as_slice(), [Out::Unload(_)]));
        let mut off = on; off.state = ModState::Unloaded;
        assert!(matches!(b.tick(&[off]).as_slice(), [Out::Job(Job::Remove { id })] if id == "x.mod"));
    }

    #[test]
    fn sources_can_be_added_and_removed_except_the_official_one() {
        let mut b = loaded();
        let out = b.add_source("https://github.com/someone/their-list");
        assert!(matches!(out.as_slice(), [Out::SaveSources(s), Out::Job(Job::FetchList { source })] if s.len() == 3 && source == "someone/their-list"));
        assert!(b.add_source("someone/their-list").is_empty() && b.status.contains("already"));
        assert!(b.add_source("http://nope.example/list.json").is_empty());
        b.select("z.mod"); // official
        assert!(b.on_event("b-remove-source", EventKind::Activated, &[]).is_empty() && b.status.contains("official"));
        b.select("x.mod");
        assert!(matches!(b.on_event("b-remove-source", EventKind::Activated, &[]).as_slice(), [Out::SaveSources(s)] if s.len() == 2));
        assert!(b.catalog.iter().all(|m| m.source != "a/one"));
        assert_eq!(Browse::new(vec![]).sources, [OFFICIAL_SOURCE]);
    }
}
