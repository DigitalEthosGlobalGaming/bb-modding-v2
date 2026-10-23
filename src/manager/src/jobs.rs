//! Background work: everything that touches the network or the disk runs on one host-managed
//! worker thread (`Cx::spawn`). The game thread sends a [`Job`] and polls for [`Done`] each frame.

use crate::catalog::{self, Asset, GitHubRepo, ListEntry, Manifest, ModList, Release, Source};
use crate::install::{self, Dirs};
use std::io::Read;
use std::sync::mpsc::{Receiver, RecvTimeoutError, Sender};
use std::time::Duration;

const MAX_JSON: u64 = 2 * 1024 * 1024;
const MAX_ZIP: u64 = 256 * 1024 * 1024;
const TIMEOUT: Duration = Duration::from_secs(20);

#[derive(Debug)]
pub enum Job {
    FetchList { source: String },
    FetchManifest { entry: ListEntry },
    Install { entry: ListEntry },
    Remove { id: String },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Step { Checking, Downloading, Verifying, Unpacking }

#[derive(Debug)]
pub enum Done {
    /// Install progress; not a final result.
    Progress { id: String, step: Step, done: u64, total: u64 },
    List { source: String, result: Result<ModList, String> },
    Manifest { id: String, result: Result<Manifest, String> },
    Installed { id: String, result: Result<install::Installed, String> },
    Removed { id: String, result: Result<(), String> },
}

fn agent() -> ureq::Agent {
    ureq::AgentBuilder::new().timeout(TIMEOUT).user_agent("bbmod-manager").redirects(5).build()
}

/// GET `url` over https and read at most `limit` bytes.
fn get(url: &str, limit: u64) -> Result<Vec<u8>, String> { get_progress(url, limit, 0, &|_, _| {}) }

/// Like [`get`], reporting (bytes so far, total) as the body arrives. `hint` is the expected size.
fn get_progress(url: &str, limit: u64, hint: u64, progress: &dyn Fn(u64, u64)) -> Result<Vec<u8>, String> {
    if !url.starts_with("https://") { return Err("Only https:// links are allowed.".into()); }
    let resp = agent().get(url).call().map_err(|e| match e {
        ureq::Error::Status(code, _) => format!("The server answered {code} for {url}"),
        e => format!("Cannot reach {url}: {e}"),
    })?;
    let total = resp.header("Content-Length").and_then(|v| v.parse::<u64>().ok()).unwrap_or(hint);
    let mut reader = resp.into_reader().take(limit + 1);
    let mut buf = Vec::new();
    let mut chunk = [0u8; 64 * 1024];
    let mut last = std::time::Instant::now();
    loop {
        let n = reader.read(&mut chunk).map_err(|e| format!("Download failed: {e}"))?;
        if n == 0 { break; }
        buf.extend_from_slice(&chunk[..n]);
        if buf.len() as u64 > limit { return Err("The download is larger than allowed.".into()); }
        if last.elapsed() >= Duration::from_millis(100) { last = std::time::Instant::now(); progress(buf.len() as u64, total); }
    }
    progress(buf.len() as u64, total.max(buf.len() as u64));
    Ok(buf)
}

fn get_text(url: &str, limit: u64) -> Result<String, String> {
    String::from_utf8(get(url, limit)?).map_err(|_| "The response is not text.".to_string())
}

fn repo_of(entry: &ListEntry) -> Result<GitHubRepo, String> {
    catalog::parse_github_repo(&entry.repo).ok_or_else(|| format!("\"{}\" is not a GitHub repository.", entry.repo))
}

pub fn fetch_list(source: &str) -> Result<ModList, String> {
    let src = Source::parse(source)?;
    ModList::parse(&get_text(&src.list_url(), MAX_JSON)?)
}

pub fn fetch_manifest(entry: &ListEntry) -> Result<Manifest, String> {
    let repo = repo_of(entry)?;
    let path = entry.mod_toml.as_deref().unwrap_or("mod.toml");
    let m = Manifest::parse(&get_text(&repo.raw_url(entry.git_ref.as_deref().unwrap_or("HEAD"), path), MAX_JSON)?)?;
    if m.id != entry.id { return Err(format!("The repository's mod.toml is for \"{}\", not \"{}\".", m.id, entry.id)); }
    Ok(m)
}

pub fn install_entry(entry: &ListEntry, dirs: &Dirs, report: &dyn Fn(Step, u64, u64)) -> Result<install::Installed, String> {
    report(Step::Checking, 0, 0);
    let manifest = fetch_manifest(entry)?;
    if catalog::compat(manifest.abi, catalog::HOST_ABI) != catalog::Compat::Compatible {
        return Err(catalog::compat_text(catalog::compat(manifest.abi, catalog::HOST_ABI), manifest.abi));
    }
    let repo = repo_of(entry)?;
    let release: Release = serde_json::from_slice(&get(&repo.release_url(entry.git_ref.as_deref()), MAX_JSON)?)
        .map_err(|e| format!("Unreadable release information: {e}"))?;
    let pick = catalog::pick_asset(&release, entry.release.as_deref(), &entry.id)?;
    let sum = match (&entry.sha256, pick.checksum) {
        (Some(s), _) => Some(s.clone()),
        (None, Some(a)) => catalog::parse_checksum(&get_text(&a.browser_download_url, 4096)?),
        (None, None) => None,
    };
    let bytes = get_progress(&asset_url(pick.zip), MAX_ZIP, pick.zip.size, &|d, t| report(Step::Downloading, d, t))?;
    report(Step::Verifying, 0, 0);
    report(Step::Unpacking, 0, 0);
    install::install_zip(&bytes, dirs, &entry.id, sum.as_deref())
}

fn asset_url(a: &Asset) -> String { a.browser_download_url.clone() }

/// Run jobs until `stop()` or the sender is dropped.
pub fn worker(jobs: Receiver<Job>, done: Sender<Done>, dirs: Dirs, stop: &dyn Fn() -> bool) {
    while !stop() {
        let job = match jobs.recv_timeout(Duration::from_millis(250)) {
            Ok(j) => j,
            Err(RecvTimeoutError::Timeout) => continue,
            Err(RecvTimeoutError::Disconnected) => return,
        };
        let out = match job {
            Job::FetchList { source } => { let result = fetch_list(&source); Done::List { source, result } }
            Job::FetchManifest { entry } => { let result = fetch_manifest(&entry); Done::Manifest { id: entry.id, result } }
            Job::Install { entry } => {
                let id = entry.id.clone();
                let tx = done.clone();
                let report = move |step: Step, d: u64, t: u64| { let _ = tx.send(Done::Progress { id: id.clone(), step, done: d, total: t }); };
                let result = install_entry(&entry, &dirs, &report);
                Done::Installed { id: entry.id, result }
            }
            Job::Remove { id } => { let result = install::remove(&dirs, &id); Done::Removed { id, result } }
        };
        if done.send(out).is_err() { return; }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Needs the network: `cargo test --release -- --ignored`.
    #[test]
    #[ignore]
    fn the_official_list_and_a_manifest_can_be_fetched_live() {
        let list = fetch_list(catalog::OFFICIAL_SOURCE).expect("official list");
        assert!(list.mods.iter().any(|m| m.id == "me.difficulty"), "{list:?}");
        let entry = list.mods.iter().find(|m| m.id == "me.difficulty").unwrap();
        let m = fetch_manifest(entry).expect("manifest");
        assert_eq!((m.id.as_str(), m.dll.as_str()), ("me.difficulty", "me_difficulty.dll"));
        assert!(fetch_list("DigitalEthosGlobalGaming/does-not-exist-xyz").is_err());
    }
}
