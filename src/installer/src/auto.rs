//! Installing the right runtime for whatever bbport the player has.
//!
//! The runtime is compiled into `bb-probe.exe`, so it must match the installed bbport release.
//! This module detects the release, refuses versions it has no runtime for (touching nothing),
//! and makes a re-install clean: a previous install is undone first so the backups always hold
//! the player's real bbport files, and updating bbport or this kit never stacks one build on another.

use crate::install::{self, Action, PlanItem, Receipt};
use crate::payload::{Detection, Payload, RuntimeInfo};
use std::path::Path;

#[derive(Debug)]
pub struct Outcome { pub receipt: Receipt, pub runtime: String, pub restored_previous: bool }

/// Where `bb-probe.exe` lives in the port (`bin` or the port itself).
pub fn probe_path(port: &Path, probe_dir: &str) -> std::path::PathBuf {
    if probe_dir.is_empty() { port.join("bb-probe.exe") } else { port.join(probe_dir).join("bb-probe.exe") }
}

fn unsupported(p: &Payload, sha: &str) -> String {
    let short = &sha[..12.min(sha.len())];
    format!(
        "This bbport version is not supported by this installer yet (bb-probe.exe {short}). Supported: {}. Nothing was changed. \
         Look for a newer installer, or use the bbport version listed above.",
        if p.labels().is_empty() { "none (this is a developer build without a payload)".to_string() } else { p.labels().join(", ") })
}

/// The earlier install, when the port holds one whose files are all still as we left them.
fn can_restore(port: &Path) -> Option<Receipt> {
    let r = install::read_receipt(port)?;
    install::changed_since(port, &r).is_empty().then_some(r)
}

/// The runtime that would be installed, as seen from a port's current state. `force` names a
/// runtime to use regardless of detection. After a restorable install the stock files are what
/// the receipt recorded, so the choice is made from those.
pub fn choose<'a>(p: &'a Payload, port: &Path, probe_dir: &str, force: Option<&str>) -> Result<&'a RuntimeInfo, String> {
    if let Some(id) = force { return p.runtime(id).ok_or_else(|| format!("No runtime named {id} in this installer.")); }
    if p.manifest.runtimes.is_empty() { return Err(unsupported(p, "")); }
    // A restorable install: judge by the original probe it replaced.
    if let Some(r) = can_restore(port) {
        let rel = if probe_dir.is_empty() { "bb-probe.exe".to_string() } else { format!("{probe_dir}/bb-probe.exe") };
        if let Some(before) = r.files.iter().find(|f| f.path == rel).and_then(|f| f.before.clone()) {
            if let Some(rt) = p.manifest.runtimes.iter().find(|rt| rt.stock_probe_sha256.iter().any(|h| h.eq_ignore_ascii_case(&before))) { return Ok(rt); }
            if p.manifest.runtimes.iter().any(|rt| rt.probe_sha256.eq_ignore_ascii_case(&before)) {
                return Err("Your earlier files were already modified; uninstall first, then run the installer again.".into());
            }
            return Err(unsupported(p, &before));
        }
    }
    match p.detect(&probe_path(port, probe_dir)) {
        Detection::Stock(r) | Detection::Integrated(r) => Ok(r),
        Detection::Unknown(sha) => Err(unsupported(p, &sha)),
        Detection::Missing => Err("bb-probe.exe was not found in that bbport folder.".into()),
    }
}

/// What an install would do, for the review page: the runtime's label, whether an earlier
/// install will be undone first, and the files.
pub fn plan(p: &Payload, port: &Path, probe_dir: &str, force: Option<&str>) -> Result<(String, bool, Vec<PlanItem>), String> {
    let rt = choose(p, port, probe_dir, force)?;
    let entries = p.entries(Some(rt))?;
    let restoring = can_restore(port).is_some();
    let mut items = install::plan(&entries, port);
    if restoring {
        // Files an earlier install put there are restored, then written again.
        for i in &mut items { if i.action == Action::Unchanged { i.action = Action::Replace; } }
    }
    Ok((rt.label.clone(), restoring, items))
}

pub fn install_auto(p: &Payload, port: &Path, probe_dir: &str, game: &str, force: Option<&str>, check_running: bool) -> Result<Outcome, String> {
    if p.is_empty() { return Err("This installer build has no payload to install.".into()); }
    let rt = choose(p, port, probe_dir, force)?;
    let entries = p.entries(Some(rt))?;
    if check_running {
        let running = install::running_processes();
        if !running.is_empty() { return Err(format!("Close the game and launcher first ({}).", running.join(", "))); }
    }
    let restored_previous = if can_restore(port).is_some() { install::uninstall(port, false)?; true } else { false };
    let receipt = install::install(&entries, port, game, false)?;
    Ok(Outcome { receipt, runtime: rt.id.clone(), restored_previous })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::payload::testing::build;
    use std::fs;
    use std::path::PathBuf;

    const STOCK_194: &[u8] = b"MZ stock 1.9.4";
    const STOCK_192: &[u8] = b"MZ stock 1.9.2";

    fn zip() -> Vec<u8> {
        build(&[("bbmods/bbmod.manager/mod.toml", b"[mod]")],
            &[("windows-v1.9.4", STOCK_194, &[("bin/bb-probe.exe", b"MZ patched 1.9.4"), ("bin/x.dll", b"d194")]),
              ("windows-v1.9.2", STOCK_192, &[("bin/bb-probe.exe", b"MZ patched 1.9.2"), ("bin/x.dll", b"d192")])])
    }
    fn port(tag: &str, probe: &[u8]) -> PathBuf {
        let p = std::env::temp_dir().join(format!("bbi-auto-{tag}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&p);
        fs::create_dir_all(p.join("bin")).unwrap();
        fs::write(p.join("bin/bb-probe.exe"), probe).unwrap();
        fs::write(p.join("bbport.ini"), b"quality").unwrap();
        p
    }

    #[test]
    fn the_runtime_matching_the_installed_bbport_is_installed_and_other_files_are_kept() {
        let bytes = zip();
        let pl = Payload::open(&bytes).unwrap();
        let p = port("match", STOCK_194);
        let out = install_auto(&pl, &p, "bin", "g", None, false).unwrap();
        assert_eq!((out.runtime.as_str(), out.restored_previous), ("windows-v1.9.4", false));
        assert_eq!(fs::read(p.join("bin/bb-probe.exe")).unwrap(), b"MZ patched 1.9.4");
        assert_eq!(fs::read(p.join("bin/x.dll")).unwrap(), b"d194");
        assert!(p.join("bbmods/bbmod.manager/mod.toml").is_file());
        assert_eq!(fs::read(p.join("bbport.ini")).unwrap(), b"quality");
        install::uninstall(&p, false).unwrap();
        assert_eq!(fs::read(p.join("bin/bb-probe.exe")).unwrap(), STOCK_194);
        let _ = fs::remove_dir_all(p);
    }

    #[test]
    fn an_unknown_bbport_version_is_refused_without_touching_anything() {
        let bytes = zip();
        let pl = Payload::open(&bytes).unwrap();
        let p = port("unknown", b"MZ stock 1.9.7 from the future");
        let e = install_auto(&pl, &p, "bin", "g", None, false).unwrap_err();
        assert!(e.contains("not supported") && e.contains("bbport windows-v1.9.4") && e.contains("Nothing was changed"), "{e}");
        assert_eq!(fs::read(p.join("bin/bb-probe.exe")).unwrap(), b"MZ stock 1.9.7 from the future");
        assert!(!p.join("bbmods").exists() && install::read_receipt(&p).is_none());
        assert!(plan(&pl, &p, "bin", None).is_err());
        assert!(install_auto(&pl, &p, "bin", "g", Some("nope"), false).is_err());
        // an explicit override is possible
        assert!(install_auto(&pl, &p, "bin", "g", Some("windows-v1.9.2"), false).is_ok());
        let _ = fs::remove_dir_all(p);
    }

    #[test]
    fn reinstalling_is_clean_and_never_stacks_one_build_on_another() {
        // The failure this exists for: bbport was updated after an older runtime had been installed.
        let bytes = zip();
        let pl = Payload::open(&bytes).unwrap();
        let p = port("repair", STOCK_192);
        install_auto(&pl, &p, "bin", "g", None, false).unwrap(); // installs the 1.9.2 runtime
        assert_eq!(fs::read(p.join("bin/bb-probe.exe")).unwrap(), b"MZ patched 1.9.2");
        let out = install_auto(&pl, &p, "bin", "g", None, false).unwrap(); // idempotent
        assert!(out.restored_previous && out.runtime == "windows-v1.9.2");
        install::uninstall(&p, false).unwrap();
        assert_eq!(fs::read(p.join("bin/bb-probe.exe")).unwrap(), STOCK_192); // the real original, not a stacked build
        install_auto(&pl, &p, "bin", "g", None, false).unwrap();
        let out = install_auto(&pl, &p, "bin", "g", Some("windows-v1.9.4"), false).unwrap();
        assert!(out.restored_previous);
        assert_eq!(fs::read(p.join("bin/x.dll")).unwrap(), b"d194");
        install::uninstall(&p, false).unwrap();
        assert_eq!(fs::read(p.join("bin/bb-probe.exe")).unwrap(), STOCK_192);
        let _ = fs::remove_dir_all(p);
    }

    #[test]
    fn a_developer_build_with_no_payload_installs_nothing_and_says_so() {
        let mut empty = vec![0x50, 0x4b, 0x05, 0x06];
        empty.extend([0u8; 18]);
        let pl = Payload::open(&empty).unwrap();
        assert!(pl.is_empty());
        let p = port("empty", STOCK_194);
        assert!(install_auto(&pl, &p, "bin", "g", None, false).unwrap_err().contains("no payload"));
        assert!(Payload::open(b"junk").is_err());
        let _ = fs::remove_dir_all(p);
    }

    #[test]
    fn payload_files_are_verified_against_their_hashes() {
        let bytes = zip();
        let pl = Payload::open(&bytes).unwrap();
        let rt = pl.runtime("windows-v1.9.4").unwrap();
        assert_eq!(pl.entries(Some(rt)).unwrap().len(), 3); // manager file + probe + dll
        assert_eq!(pl.entries(None).unwrap().len(), 1);
        let mut bad = Payload::open(&bytes).unwrap();
        bad.manifest.runtimes[0].files[0].sha256 = "00".repeat(32);
        let rt = bad.manifest.runtimes[0].clone();
        assert!(bad.entries(Some(&rt)).is_err());
    }
}
