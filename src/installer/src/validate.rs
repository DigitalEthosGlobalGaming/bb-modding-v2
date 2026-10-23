//! Checks on the two folders the player picks: the game dump and the installed bbport.
//! Mirrors `validate()` in `src/scripts/toolkit.py`.

use std::fs;
use std::path::{Path, PathBuf};

pub const TITLE_ID: &str = "CUSA03173";
pub const APP_VER: &str = "01.09";

#[derive(Debug, PartialEq, Eq)]
pub struct GameInfo {
    /// The `CUSA03173/uroot` folder.
    pub root: PathBuf,
}

#[derive(Debug, PartialEq, Eq)]
pub struct PortInfo {
    pub dir: PathBuf,
    /// Where `bb-probe.exe` lives, relative to the port (`bin` or the port itself).
    pub probe_dir: &'static str,
    pub has_bbmods: bool,
}

/// Read the string entries of a PARAM.SFO.
pub fn read_sfo(data: &[u8]) -> Result<Vec<(String, String)>, String> {
    let bad = || "PARAM.SFO is not valid.".to_string();
    if data.len() < 20 || &data[..4] != b"\0PSF" { return Err(bad()); }
    let u32at = |o: usize| data.get(o..o + 4).map(|b| u32::from_le_bytes(b.try_into().unwrap()));
    let u16at = |o: usize| data.get(o..o + 2).map(|b| u16::from_le_bytes(b.try_into().unwrap()));
    let (keys, values, count) = (u32at(8).ok_or_else(bad)? as usize, u32at(12).ok_or_else(bad)? as usize, u32at(16).ok_or_else(bad)? as usize);
    if count > 4096 || 20 + 16 * count > data.len() { return Err(bad()); }
    let mut out = Vec::new();
    for i in 0..count {
        let e = 20 + i * 16;
        let (key, fmt) = (u16at(e).ok_or_else(bad)? as usize, u16at(e + 2).ok_or_else(bad)?);
        let (size, max, off) = (u32at(e + 4).ok_or_else(bad)? as usize, u32at(e + 8).ok_or_else(bad)? as usize, u32at(e + 12).ok_or_else(bad)? as usize);
        let start = keys + key;
        let end = data.get(start..).and_then(|d| d.iter().position(|&b| b == 0)).map(|p| start + p).ok_or_else(bad)?;
        if values + off + size > data.len() || size > max { return Err(bad()); }
        if fmt == 0x204 {
            let name = String::from_utf8_lossy(&data[start..end]).into_owned();
            let val = String::from_utf8_lossy(&data[values + off..values + off + size]).trim_end_matches('\0').to_owned();
            out.push((name, val));
        }
    }
    Ok(out)
}

/// Accepts the game parent folder, `CUSA03173`, or `uroot` itself.
pub fn validate_game(dir: &Path) -> Result<GameInfo, String> {
    let candidates = [dir.join(TITLE_ID).join("uroot"), dir.join("uroot"), dir.to_path_buf()];
    let root = candidates.into_iter().find(|c| c.join("sce_sys").join("param.sfo").is_file())
        .ok_or_else(|| format!("Pick the game folder that contains {TITLE_ID}\\uroot."))?;
    let sfo = read_sfo(&fs::read(root.join("sce_sys").join("param.sfo")).map_err(|e| format!("Cannot read PARAM.SFO: {e}"))?)?;
    let get = |k: &str| sfo.iter().find(|(n, _)| n == k).map(|(_, v)| v.as_str());
    if get("TITLE_ID") != Some(TITLE_ID) || get("APP_VER") != Some(APP_VER) {
        return Err(format!("Expected Bloodborne {TITLE_ID} update 1.09; found {} {}.", get("TITLE_ID").unwrap_or("?"), get("APP_VER").unwrap_or("?")));
    }
    if !root.join("eboot.bin").is_file() { return Err("The game folder is missing eboot.bin.".into()); }
    let assets = root.join("dvdroot_ps4");
    for rel in ["param/gameparam/gameparam.parambnd.dcx", "msg/engus/item.msgbnd.dcx", "action/script/c0000.hks"] {
        if !assets.join(rel).is_file() { return Err(format!("The game folder is missing {rel}.")); }
    }
    Ok(GameInfo { root })
}

pub fn validate_port(dir: &Path) -> Result<PortInfo, String> {
    if !dir.is_dir() || !dir.join("Bloodborne.exe").is_file() {
        return Err("Pick the installed bbport folder (the one that contains Bloodborne.exe).".into());
    }
    let probe_dir = ["bin", ""].into_iter().find(|d| {
        fs::read(dir.join(d).join("bb-probe.exe")).is_ok_and(|b| b.starts_with(b"MZ"))
    }).ok_or("The port folder has no bb-probe.exe; install bbport first.")?;
    Ok(PortInfo { dir: dir.to_path_buf(), probe_dir: if probe_dir.is_empty() { "" } else { "bin" }, has_bbmods: dir.join("bbmods").is_dir() })
}

#[cfg(test)]
pub fn sfo_bytes(entries: &[(&str, &str)]) -> Vec<u8> {
    let mut keys: Vec<u8> = Vec::new();
    let mut values: Vec<u8> = Vec::new();
    let mut index: Vec<u8> = Vec::new();
    for (k, v) in entries {
        let (ko, vo) = (keys.len() as u16, values.len() as u32);
        keys.extend(k.as_bytes()); keys.push(0);
        let mut val = v.as_bytes().to_vec(); val.push(0);
        let size = val.len() as u32;
        values.extend(&val);
        index.extend(ko.to_le_bytes()); index.extend(0x204u16.to_le_bytes());
        index.extend(size.to_le_bytes()); index.extend(size.to_le_bytes()); index.extend(vo.to_le_bytes());
    }
    let key_off = 20 + index.len() as u32;
    let val_off = key_off + keys.len() as u32;
    let mut out = b"\0PSF".to_vec();
    out.extend(0x101u32.to_le_bytes()); out.extend(key_off.to_le_bytes()); out.extend(val_off.to_le_bytes());
    out.extend((entries.len() as u32).to_le_bytes());
    out.extend(index); out.extend(keys); out.extend(values);
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tree(tag: &str) -> PathBuf {
        let root = std::env::temp_dir().join(format!("bbi-validate-{tag}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(&root).unwrap();
        root
    }
    fn write(p: PathBuf, data: &[u8]) { fs::create_dir_all(p.parent().unwrap()).unwrap(); fs::write(p, data).unwrap(); }

    fn make_game(root: &Path, ver: &str) {
        let u = root.join("CUSA03173/uroot");
        write(u.join("sce_sys/param.sfo"), &sfo_bytes(&[("TITLE_ID", "CUSA03173"), ("APP_VER", ver)]));
        write(u.join("eboot.bin"), b"x");
        for rel in ["param/gameparam/gameparam.parambnd.dcx", "msg/engus/item.msgbnd.dcx", "action/script/c0000.hks"] {
            write(u.join("dvdroot_ps4").join(rel), b"x");
        }
    }

    #[test]
    fn game_folder_may_be_the_parent_title_or_uroot_and_must_be_update_1_09() {
        let root = tree("game");
        make_game(&root, "01.09");
        let uroot = root.join("CUSA03173/uroot");
        for pick in [root.clone(), root.join("CUSA03173"), uroot.clone()] {
            assert_eq!(validate_game(&pick).unwrap().root, uroot);
        }
        assert!(validate_game(&root.join("nowhere")).is_err());
        fs::remove_file(uroot.join("eboot.bin")).unwrap();
        assert!(validate_game(&root).unwrap_err().contains("eboot.bin"));
        make_game(&root, "01.00");
        assert!(validate_game(&root).unwrap_err().contains("01.00"));
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn port_folder_needs_the_exe_and_a_probe_in_bin_or_the_root() {
        let root = tree("port");
        assert!(validate_port(&root).is_err());
        write(root.join("Bloodborne.exe"), b"MZ");
        assert!(validate_port(&root).unwrap_err().contains("bb-probe"));
        write(root.join("bin/bb-probe.exe"), b"not an exe");
        assert!(validate_port(&root).is_err());
        write(root.join("bin/bb-probe.exe"), b"MZ....");
        let p = validate_port(&root).unwrap();
        assert_eq!((p.probe_dir, p.has_bbmods), ("bin", false));
        fs::create_dir(root.join("bbmods")).unwrap();
        assert!(validate_port(&root).unwrap().has_bbmods);
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn corrupt_sfo_is_rejected() {
        assert!(read_sfo(b"nope").is_err());
        let mut b = sfo_bytes(&[("TITLE_ID", "X")]);
        b.truncate(30);
        assert!(read_sfo(&b).is_err());
    }
}
