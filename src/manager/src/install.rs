//! Installing a mod from a release ZIP into the mods directory, and removing it again.
//!
//! The archive is verified (optional SHA-256), extracted into a staging folder *outside* the
//! mods directory (the host scans `bbmods/` for new folders every few seconds, so a half-written
//! mod must never appear there), checked, and then swapped into place with renames. A previous
//! install of the same mod is kept under `previous/` for rollback. Nothing here touches the
//! mod's settings, which live in `<bbmods>/../config`.

use crate::catalog::{valid_id, Manifest};
use sha2::{Digest, Sha256};
use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};

/// Most files and bytes one archive may expand to.
pub const MAX_FILES: usize = 4000;
pub const MAX_UNPACKED: u64 = 512 * 1024 * 1024;

/// Folders the manager uses, derived from the mods directory.
#[derive(Clone, Debug)]
pub struct Dirs { pub mods: PathBuf, pub staging: PathBuf, pub previous: PathBuf }

impl Dirs {
    /// `mods` is `<port>/bbmods`; working folders sit in `<port>/mod-data/bbmod.manager`.
    pub fn new(mods: &Path, own_id: &str) -> Dirs {
        let work = mods.parent().unwrap_or(mods).join("mod-data").join(own_id);
        Dirs { mods: mods.to_path_buf(), staging: work.join("staging"), previous: work.join("previous") }
    }
}

#[derive(Debug, PartialEq, Eq)]
pub struct Installed { pub id: String, pub version: (u16, u16, u16), pub replaced: bool }

pub fn sha256_hex(data: &[u8]) -> String {
    Sha256::digest(data).iter().map(|b| format!("{b:02x}")).collect()
}

fn err<T>(s: impl Into<String>) -> Result<T, String> { Err(s.into()) }
fn io_err(what: &str, e: io::Error) -> String { format!("{what}: {e}") }

/// Install `zip_bytes` as mod `id`. `expected_sha256` (hex) is checked when given.
/// The archive holds the mod either at its root or inside a single top-level folder.
pub fn install_zip(zip_bytes: &[u8], dirs: &Dirs, id: &str, expected_sha256: Option<&str>) -> Result<Installed, String> {
    if !valid_id(id) { return err(format!("Bad mod id \"{id}\".")); }
    if let Some(want) = expected_sha256 {
        let got = sha256_hex(zip_bytes);
        if !got.eq_ignore_ascii_case(want.trim()) { return err("The download does not match its published checksum; nothing was installed."); }
    }
    let mut archive = zip::ZipArchive::new(io::Cursor::new(zip_bytes)).map_err(|e| format!("Not a valid ZIP: {e}"))?;
    if archive.len() > MAX_FILES { return err("The archive has too many files."); }

    let root = find_root(&mut archive)?;
    let stage = dirs.staging.join(id);
    if stage.exists() { fs::remove_dir_all(&stage).map_err(|e| io_err("Cannot clear the staging folder", e))?; }
    fs::create_dir_all(&stage).map_err(|e| io_err("Cannot create the staging folder", e))?;
    let result = extract(&mut archive, &root, &stage).and_then(|()| check_stage(&stage, id)).and_then(|m| swap_in(dirs, id, &stage).map(|replaced| (m, replaced)));
    if stage.exists() { let _ = fs::remove_dir_all(&stage); }
    let (manifest, replaced) = result?;
    Ok(Installed { id: id.into(), version: manifest.version, replaced })
}

/// Folder prefix inside the archive that holds `mod.toml` ("" or "name/").
fn find_root<R: Read + io::Seek>(a: &mut zip::ZipArchive<R>) -> Result<String, String> {
    let mut found: Option<String> = None;
    for i in 0..a.len() {
        let f = a.by_index(i).map_err(|e| format!("Bad ZIP entry: {e}"))?;
        let Some(name) = f.enclosed_name() else { return err("The archive contains an unsafe path."); };
        if f.is_file() && name.file_name().is_some_and(|n| n == "mod.toml") {
            let depth = name.components().count();
            if depth > 2 { continue; }
            let prefix = if depth == 2 { format!("{}/", name.components().next().unwrap().as_os_str().to_string_lossy()) } else { String::new() };
            if found.as_ref().is_some_and(|p| p.len() <= prefix.len()) { continue; }
            found = Some(prefix);
        }
    }
    found.ok_or_else(|| "The archive has no mod.toml at its top level.".to_string())
}

fn extract<R: Read + io::Seek>(a: &mut zip::ZipArchive<R>, root: &str, stage: &Path) -> Result<(), String> {
    let mut total = 0u64;
    for i in 0..a.len() {
        let mut f = a.by_index(i).map_err(|e| format!("Bad ZIP entry: {e}"))?;
        let Some(name) = f.enclosed_name() else { return err("The archive contains an unsafe path."); };
        if f.unix_mode().is_some_and(|m| m & 0o170000 == 0o120000) { return err("The archive contains a symbolic link."); }
        let Ok(rel) = name.strip_prefix(root.trim_end_matches('/')) else { continue }; // outside the mod folder
        if rel.as_os_str().is_empty() { continue; }
        let out = stage.join(rel);
        if f.is_dir() { fs::create_dir_all(&out).map_err(|e| io_err("Cannot create a folder", e))?; continue; }
        total = total.saturating_add(f.size());
        if total > MAX_UNPACKED { return err("The archive is too large to unpack."); }
        if let Some(p) = out.parent() { fs::create_dir_all(p).map_err(|e| io_err("Cannot create a folder", e))?; }
        let mut w = fs::File::create(&out).map_err(|e| io_err("Cannot write a file", e))?;
        io::copy(&mut f.by_ref().take(MAX_UNPACKED), &mut w).map_err(|e| io_err("Cannot unpack a file", e))?;
    }
    Ok(())
}

/// The staged folder must be a well-formed, non-managing mod with the expected id and its DLL.
fn check_stage(stage: &Path, id: &str) -> Result<Manifest, String> {
    let text = fs::read_to_string(stage.join("mod.toml")).map_err(|e| io_err("Cannot read mod.toml", e))?;
    let m = Manifest::parse(&text)?;
    if m.id != id { return err(format!("mod.toml declares \"{}\" but \"{id}\" was expected.", m.id)); }
    if m.manage { return err("Mods that manage other mods cannot be installed from a list."); }
    if !m.dll.is_empty() && !stage.join(&m.dll).is_file() { return err(format!("The archive is missing {}.", m.dll)); }
    Ok(m)
}

/// Move the staged mod into `bbmods/<id>`, keeping the old install under `previous/`.
fn swap_in(dirs: &Dirs, id: &str, stage: &Path) -> Result<bool, String> {
    fs::create_dir_all(&dirs.mods).map_err(|e| io_err("Cannot create the mods folder", e))?;
    let target = dirs.mods.join(id);
    let mut replaced = false;
    let backup = dirs.previous.join(id);
    if target.exists() {
        fs::create_dir_all(&dirs.previous).map_err(|e| io_err("Cannot create the backup folder", e))?;
        if backup.exists() { fs::remove_dir_all(&backup).map_err(|e| io_err("Cannot clear the old backup", e))?; }
        fs::rename(&target, &backup).map_err(|e| format!("Cannot replace the installed mod (is it still loaded?): {e}"))?;
        replaced = true;
    }
    if let Err(e) = fs::rename(stage, &target) {
        if replaced { let _ = fs::rename(&backup, &target); } // put the old version back
        return err(format!("Cannot move the new files into place: {e}"));
    }
    Ok(replaced)
}

/// Remove an installed mod, keeping a copy under `previous/`.
pub fn remove(dirs: &Dirs, id: &str) -> Result<(), String> {
    if !valid_id(id) { return err(format!("Bad mod id \"{id}\".")); }
    let target = dirs.mods.join(id);
    if !target.is_dir() { return err("That mod is not installed."); }
    fs::create_dir_all(&dirs.previous).map_err(|e| io_err("Cannot create the backup folder", e))?;
    let backup = dirs.previous.join(id);
    if backup.exists() { fs::remove_dir_all(&backup).map_err(|e| io_err("Cannot clear the old backup", e))?; }
    fs::rename(&target, &backup).map_err(|e| format!("Cannot remove the mod (is it still loaded?): {e}"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    fn zip_of(files: &[(&str, &[u8])]) -> Vec<u8> {
        let mut w = zip::ZipWriter::new(io::Cursor::new(Vec::new()));
        for (n, d) in files {
            w.start_file(*n, zip::write::SimpleFileOptions::default()).unwrap();
            w.write_all(d).unwrap();
        }
        w.finish().unwrap().into_inner()
    }
    const TOML: &str = "[mod]\nid=\"a.mod\"\nname=\"A\"\nversion=\"1.2.3\"\nabi=\"1.1\"\ndll=\"a.dll\"\n";
    fn dirs(tag: &str) -> (Dirs, PathBuf) {
        let root = std::env::temp_dir().join(format!("bbm-install-{tag}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(root.join("bbmods")).unwrap();
        (Dirs::new(&root.join("bbmods"), "bbmod.manager"), root)
    }

    #[test]
    fn installs_from_root_or_a_single_folder_and_reports_replacement() {
        let (d, root) = dirs("ok");
        let flat = zip_of(&[("mod.toml", TOML.as_bytes()), ("a.dll", b"one")]);
        assert_eq!(install_zip(&flat, &d, "a.mod", None).unwrap(), Installed { id: "a.mod".into(), version: (1, 2, 3), replaced: false });
        assert_eq!(fs::read(d.mods.join("a.mod/a.dll")).unwrap(), b"one");
        let nested = zip_of(&[("a.mod/mod.toml", TOML.as_bytes()), ("a.mod/a.dll", b"two"), ("a.mod/content/x.bin", b"x")]);
        assert!(install_zip(&nested, &d, "a.mod", Some(&sha256_hex(&nested))).unwrap().replaced);
        assert_eq!(fs::read(d.mods.join("a.mod/a.dll")).unwrap(), b"two");
        assert_eq!(fs::read(d.mods.join("a.mod/content/x.bin")).unwrap(), b"x");
        assert_eq!(fs::read(d.previous.join("a.mod/a.dll")).unwrap(), b"one"); // old version kept
        assert!(!d.staging.join("a.mod").exists());
        remove(&d, "a.mod").unwrap();
        assert!(!d.mods.join("a.mod").exists() && d.previous.join("a.mod/a.dll").exists());
        assert!(remove(&d, "a.mod").is_err());
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn rejects_bad_archives_without_touching_the_install() {
        let (d, root) = dirs("bad");
        let good = zip_of(&[("mod.toml", TOML.as_bytes()), ("a.dll", b"one")]);
        install_zip(&good, &d, "a.mod", None).unwrap();
        let cases: Vec<(&str, Vec<u8>, Option<String>)> = vec![
            ("checksum", good.clone(), Some("00".repeat(32))),
            ("traversal", zip_of(&[("mod.toml", TOML.as_bytes()), ("a.dll", b"x"), ("../evil.txt", b"x")]), None),
            ("no manifest", zip_of(&[("a.dll", b"x")]), None),
            ("wrong id", zip_of(&[("mod.toml", TOML.replace("a.mod", "b.mod").as_bytes()), ("a.dll", b"x")]), None),
            ("manage", zip_of(&[("mod.toml", format!("{TOML}manage=true\n").as_bytes()), ("a.dll", b"x")]), None),
            ("missing dll", zip_of(&[("mod.toml", TOML.as_bytes())]), None),
            ("not a zip", b"hello".to_vec(), None),
        ];
        for (name, bytes, sum) in cases {
            assert!(install_zip(&bytes, &d, "a.mod", sum.as_deref()).is_err(), "{name} should fail");
            assert_eq!(fs::read(d.mods.join("a.mod/a.dll")).unwrap(), b"one", "{name} must leave the install alone");
        }
        assert!(install_zip(&good, &d, "../a", None).is_err());
        assert!(!root.join("evil.txt").exists() && !d.mods.parent().unwrap().join("evil.txt").exists());
        let _ = fs::remove_dir_all(root);
    }
}
