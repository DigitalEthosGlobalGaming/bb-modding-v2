//! Installing the embedded payload into a bbport folder, with hash-recorded backups and rollback.
//!
//! Same model as `deploy-runtime`: every file the install replaces is copied to a backup folder
//! first, every file's before/after hash is recorded in a receipt, and an uninstall restores
//! the previous files, refusing to touch any file that changed since the install. Saves, the
//! `user/` folder, `config/` and `bbport.ini` are never part of a payload.

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::fs;
use std::io::{self, Read};
use std::path::{Component, Path, PathBuf};

pub const RECEIPT: &str = "bb-modding-install.json";
pub const BACKUPS: &str = "bb-modding-backup";
/// Paths a payload may never write to.
const PROTECTED: [&str; 4] = ["user", "config", "mod-data", "bbport.ini"];
const MAX_FILE: u64 = 512 * 1024 * 1024;

/// The payload embedded at build time (see `build.rs`).
pub fn embedded_payload() -> &'static [u8] { include_bytes!(concat!(env!("OUT_DIR"), "/payload.zip")) }

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Entry { pub path: String, pub data: Vec<u8> }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action { Add, Replace, Unchanged }

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PlanItem { pub path: String, pub action: Action, pub size: u64 }

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct FileRecord { pub path: String, pub before: Option<String>, pub after: String }

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct Receipt {
    pub installer_version: String,
    pub installed_at: String,
    pub game: String,
    /// Backup folder, relative to the port.
    pub backup: String,
    pub files: Vec<FileRecord>,
}

pub fn sha256_hex(data: &[u8]) -> String { Sha256::digest(data).iter().map(|b| format!("{b:02x}")).collect() }
fn sha_file(p: &Path) -> io::Result<String> { Ok(sha256_hex(&fs::read(p)?)) }

/// Read and sanity-check a payload archive.
pub fn read_payload(zip_bytes: &[u8]) -> Result<Vec<Entry>, String> {
    let mut a = zip::ZipArchive::new(io::Cursor::new(zip_bytes)).map_err(|e| format!("The installer payload is damaged: {e}"))?;
    let mut out = Vec::new();
    for i in 0..a.len() {
        let mut f = a.by_index(i).map_err(|e| format!("The installer payload is damaged: {e}"))?;
        if f.is_dir() { continue; }
        let name = f.enclosed_name().ok_or("The installer payload contains an unsafe path.")?;
        let rel = rel_string(&name)?;
        if f.size() > MAX_FILE { return Err(format!("{rel} is too large.")); }
        let mut data = Vec::with_capacity(f.size() as usize);
        f.by_ref().take(MAX_FILE).read_to_end(&mut data).map_err(|e| format!("Cannot read {rel}: {e}"))?;
        out.push(Entry { path: rel, data });
    }
    Ok(out)
}

fn rel_string(p: &Path) -> Result<String, String> {
    let mut parts = Vec::new();
    for c in p.components() {
        match c { Component::Normal(s) => parts.push(s.to_string_lossy().into_owned()), _ => return Err(format!("Unsafe payload path {}.", p.display())) }
    }
    let first = parts.first().ok_or("Empty payload path.")?.to_ascii_lowercase();
    if PROTECTED.contains(&first.as_str()) || first == BACKUPS || parts.last().is_some_and(|l| l == RECEIPT) {
        return Err(format!("The payload may not write to {}.", parts.join("/")));
    }
    Ok(parts.join("/"))
}

fn dest(port: &Path, rel: &str) -> PathBuf { rel.split('/').fold(port.to_path_buf(), |p, s| p.join(s)) }

/// What installing `entries` into `port` would do.
pub fn plan(entries: &[Entry], port: &Path) -> Vec<PlanItem> {
    entries.iter().map(|e| {
        let d = dest(port, &e.path);
        let action = match fs::read(&d) { Ok(cur) if cur == e.data => Action::Unchanged, Ok(_) => Action::Replace, Err(_) => Action::Add };
        PlanItem { path: e.path.clone(), action, size: e.data.len() as u64 }
    }).collect()
}

pub fn read_receipt(port: &Path) -> Option<Receipt> {
    serde_json::from_slice(&fs::read(port.join(RECEIPT)).ok()?).ok()
}

/// Names of game or launcher processes that would hold files open.
pub fn running_processes() -> Vec<String> {
    #[cfg(windows)]
    {
        let Ok(out) = std::process::Command::new("tasklist").args(["/FO", "CSV", "/NH"]).output() else { return vec![] };
        let text = String::from_utf8_lossy(&out.stdout).to_ascii_lowercase();
        ["bb-probe.exe", "bloodborne.exe", "play bloodborne.exe"].iter().filter(|n| text.contains(&format!("\"{n}\""))).map(|s| s.to_string()).collect()
    }
    #[cfg(not(windows))]
    { vec![] }
}

fn timestamp() -> String {
    let secs = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_secs());
    let (days, rem) = (secs / 86400, secs % 86400);
    // Civil date from days since 1970-01-01.
    let z = days as i64 + 719468;
    let era = z.div_euclid(146097);
    let doe = z.rem_euclid(146097);
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let (y, doy) = (yoe + era * 400, doe - (365 * yoe + yoe / 4 - yoe / 100));
    let mp = (5 * doy + 2) / 153;
    let (d, m) = (doy - (153 * mp + 2) / 5 + 1, if mp < 10 { mp + 3 } else { mp - 9 });
    let y = if m <= 2 { y + 1 } else { y };
    format!("{y:04}{m:02}{d:02}T{:02}{:02}{:02}Z", rem / 3600, rem % 3600 / 60, rem % 60)
}

fn write_atomic(path: &Path, data: &[u8]) -> io::Result<()> {
    if let Some(p) = path.parent() { fs::create_dir_all(p)?; }
    let tmp = path.with_extension(format!("{}.bbtmp", path.extension().and_then(|e| e.to_str()).unwrap_or("")));
    fs::write(&tmp, data)?;
    fs::rename(&tmp, path).inspect_err(|_| { let _ = fs::remove_file(&tmp); })
}

/// Install `entries` into `port`. `game` is recorded in the receipt. Returns the receipt.
pub fn install(entries: &[Entry], port: &Path, game: &str, check_running: bool) -> Result<Receipt, String> {
    if entries.is_empty() { return Err("This installer build has no payload to install.".into()); }
    if check_running {
        let running = running_processes();
        if !running.is_empty() { return Err(format!("Close the game and launcher first ({}).", running.join(", "))); }
    }
    let stamp = timestamp();
    let backup_rel = format!("{BACKUPS}/{stamp}");
    let backup = dest(port, &backup_rel);
    let mut records = Vec::new();
    // Preflight and back up before touching anything.
    for e in entries {
        let d = dest(port, &e.path);
        if d.exists() && !d.is_file() { return Err(format!("Expected a file at {}.", d.display())); }
        let before = if d.is_file() { Some(sha_file(&d).map_err(|e| format!("Cannot read {}: {e}", d.display()))?) } else { None };
        if before.is_some() {
            let saved = dest(&backup, &e.path);
            if let Some(p) = saved.parent() { fs::create_dir_all(p).map_err(|e| format!("Cannot create the backup folder: {e}"))?; }
            fs::copy(&d, &saved).map_err(|e| format!("Cannot back up {}: {e}", e_path(&d)))?;
        }
        records.push(FileRecord { path: e.path.clone(), before, after: sha256_hex(&e.data) });
    }
    let receipt = Receipt { installer_version: env!("CARGO_PKG_VERSION").into(), installed_at: stamp, game: game.into(), backup: backup_rel, files: records };
    fs::create_dir_all(&backup).map_err(|e| format!("Cannot create the backup folder: {e}"))?;
    let json = serde_json::to_vec_pretty(&receipt).map_err(|e| e.to_string())?;
    write_atomic(&backup.join("receipt.json"), &json).map_err(|e| format!("Cannot write the backup record: {e}"))?;

    let mut written = 0;
    for e in entries {
        if let Err(err) = write_atomic(&dest(port, &e.path), &e.data) {
            let _ = restore(port, &receipt, &receipt.files[..=written]); // put back what was already replaced
            return Err(format!("Cannot write {}: {err}. Nothing was left half-installed.", e.path));
        }
        written += 1;
    }
    write_atomic(&port.join(RECEIPT), &json).map_err(|e| format!("Installed, but cannot write the receipt: {e}"))?;
    Ok(receipt)
}

fn e_path(p: &Path) -> String { p.display().to_string() }

/// Files of `receipt` that no longer hold the content the install wrote.
fn changed_since(port: &Path, receipt: &Receipt) -> Vec<String> {
    receipt.files.iter().filter(|r| sha_file(&dest(port, &r.path)).ok().as_deref() != Some(r.after.as_str())).map(|r| r.path.clone()).collect()
}

/// Put back the previous version of each record (delete files the install created).
fn restore(port: &Path, receipt: &Receipt, records: &[FileRecord]) -> Result<(), String> {
    let backup = dest(port, &receipt.backup);
    for r in records {
        let d = dest(port, &r.path);
        match &r.before {
            Some(h) => {
                let saved = fs::read(dest(&backup, &r.path)).map_err(|e| format!("Cannot read the backup of {}: {e}", r.path))?;
                if &sha256_hex(&saved) != h { return Err(format!("The backup of {} is damaged; it was not restored.", r.path)); }
                write_atomic(&d, &saved).map_err(|e| format!("Cannot restore {}: {e}", r.path))?;
            }
            None => { let _ = fs::remove_file(&d); }
        }
    }
    Ok(())
}

/// Undo the last install. Files changed since then are preserved: the call fails and lists them.
pub fn uninstall(port: &Path, check_running: bool) -> Result<usize, String> {
    let receipt = read_receipt(port).ok_or("No bb-modding installation was found in this folder.")?;
    if check_running {
        let running = running_processes();
        if !running.is_empty() { return Err(format!("Close the game and launcher first ({}).", running.join(", "))); }
    }
    // Verify every backup before changing anything.
    let backup = dest(port, &receipt.backup);
    for r in &receipt.files {
        if let Some(h) = &r.before {
            let ok = fs::read(dest(&backup, &r.path)).is_ok_and(|b| &sha256_hex(&b) == h);
            if !ok { return Err(format!("The backup of {} is missing or damaged; nothing was changed.", r.path)); }
        }
    }
    let changed = changed_since(port, &receipt);
    if !changed.is_empty() {
        return Err(format!("These files changed after the install and were left alone: {}", changed.join(", ")));
    }
    restore(port, &receipt, &receipt.files)?;
    let _ = fs::remove_file(port.join(RECEIPT));
    // Remove folders the install created, if they are now empty.
    let mut dirs: Vec<PathBuf> = receipt.files.iter().filter(|r| r.before.is_none()).flat_map(|r| dest(port, &r.path).ancestors().skip(1).take_while(|a| *a != port).map(Path::to_path_buf).collect::<Vec<_>>()).collect();
    dirs.sort_by_key(|d| std::cmp::Reverse(d.components().count()));
    dirs.dedup();
    for d in dirs { if d != port { let _ = fs::remove_dir(&d); } }
    Ok(receipt.files.len())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    fn payload(files: &[(&str, &[u8])]) -> Vec<u8> {
        let mut w = zip::ZipWriter::new(io::Cursor::new(Vec::new()));
        for (n, d) in files { w.start_file(*n, zip::write::SimpleFileOptions::default()).unwrap(); w.write_all(d).unwrap(); }
        w.finish().unwrap().into_inner()
    }
    fn port(tag: &str) -> PathBuf {
        let p = std::env::temp_dir().join(format!("bbi-install-{tag}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&p);
        fs::create_dir_all(p.join("bin")).unwrap();
        fs::write(p.join("bin/bb-probe.exe"), b"MZ old").unwrap();
        fs::write(p.join("bbport.ini"), b"quality=high").unwrap();
        p
    }

    #[test]
    fn plan_install_and_uninstall_round_trip_leaves_user_files_alone() {
        let p = port("round");
        let entries = read_payload(&payload(&[("bin/bb-probe.exe", b"MZ new"), ("bbmods/bbmod.manager/mod.toml", b"[mod]"), ("bbmods/bbmod.manager/m.dll", b"dll")])).unwrap();
        assert_eq!(plan(&entries, &p).iter().map(|i| i.action).collect::<Vec<_>>(), [Action::Replace, Action::Add, Action::Add]);
        let r = install(&entries, &p, "D:/game", false).unwrap();
        assert_eq!(fs::read(p.join("bin/bb-probe.exe")).unwrap(), b"MZ new");
        assert_eq!(fs::read(p.join("bbmods/bbmod.manager/m.dll")).unwrap(), b"dll");
        assert_eq!(fs::read(p.join("bbport.ini")).unwrap(), b"quality=high");
        assert_eq!(read_receipt(&p).unwrap(), r);
        assert!(plan(&entries, &p).iter().all(|i| i.action == Action::Unchanged));
        assert_eq!(fs::read(p.join(&r.backup).join("bin/bb-probe.exe")).unwrap(), b"MZ old");
        assert_eq!(uninstall(&p, false).unwrap(), 3);
        assert_eq!(fs::read(p.join("bin/bb-probe.exe")).unwrap(), b"MZ old");
        assert!(!p.join("bbmods").exists() && !p.join(RECEIPT).exists());
        assert_eq!(fs::read(p.join("bbport.ini")).unwrap(), b"quality=high");
        assert!(uninstall(&p, false).is_err());
        let _ = fs::remove_dir_all(p);
    }

    #[test]
    fn uninstall_refuses_files_changed_since_the_install() {
        let p = port("changed");
        let entries = read_payload(&payload(&[("bin/bb-probe.exe", b"MZ new"), ("bbmods/x/mod.toml", b"a")])).unwrap();
        install(&entries, &p, "g", false).unwrap();
        fs::write(p.join("bbmods/x/mod.toml"), b"edited by the user").unwrap();
        let e = uninstall(&p, false).unwrap_err();
        assert!(e.contains("bbmods/x/mod.toml"), "{e}");
        assert_eq!(fs::read(p.join("bin/bb-probe.exe")).unwrap(), b"MZ new"); // nothing was restored
        assert!(p.join(RECEIPT).exists());
        let _ = fs::remove_dir_all(p);
    }

    #[test]
    fn a_second_install_upgrades_and_keeps_its_own_backup() {
        let p = port("upgrade");
        let v1 = read_payload(&payload(&[("bin/bb-probe.exe", b"MZ v1")])).unwrap();
        let v2 = read_payload(&payload(&[("bin/bb-probe.exe", b"MZ v2")])).unwrap();
        let r1 = install(&v1, &p, "g", false).unwrap();
        std::thread::sleep(std::time::Duration::from_millis(1100)); // backups are named by second
        let r2 = install(&v2, &p, "g", false).unwrap();
        assert_ne!(r1.backup, r2.backup);
        assert_eq!(fs::read(p.join(&r1.backup).join("bin/bb-probe.exe")).unwrap(), b"MZ old");
        assert_eq!(fs::read(p.join(&r2.backup).join("bin/bb-probe.exe")).unwrap(), b"MZ v1");
        uninstall(&p, false).unwrap();
        assert_eq!(fs::read(p.join("bin/bb-probe.exe")).unwrap(), b"MZ v1"); // back to the state before v2
        let _ = fs::remove_dir_all(p);
    }

    #[test]
    fn payloads_cannot_escape_or_touch_user_data() {
        for bad in ["../x.dll", "user/save.dat", "config/a.toml", "bbport.ini", "mod-data/a", BACKUPS, "bin/bb-modding-install.json"] {
            let z = payload(&[(bad, b"x")]);
            assert!(read_payload(&z).is_err(), "{bad} should be rejected");
        }
        assert!(read_payload(b"not a zip").is_err());
        assert!(install(&[], &port("empty"), "g", false).is_err()); // the empty embedded payload
        assert!(read_payload(embedded_payload()).is_ok());
    }

    #[test]
    fn timestamps_are_utc_and_sortable() {
        let t = timestamp();
        assert!(t.len() == 16 && t.ends_with('Z') && t.as_bytes()[8] == b'T', "{t}");
    }
}
