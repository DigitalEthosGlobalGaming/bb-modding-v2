//! The installer's payload: the Mod Manager plus one patched bbport runtime per supported bbport
//! version. A runtime is part of `bb-probe.exe` itself, so it only works with the bbport release
//! it was built from; the payload therefore carries several and the installer picks by detecting
//! which release is installed (see [`detect`]).
//!
//! Layout of `payload.zip`: `payload.json` plus `blobs/<sha256>` (every distinct file once).

use crate::install::{rel_string, sha256_hex, sha_file, Entry};
use serde::Deserialize;
use std::io::{self, Read};
use std::path::Path;

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
pub struct FileRef { pub path: String, pub sha256: String }

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
pub struct RuntimeInfo {
    pub id: String,
    pub label: String,
    /// SHA-256 of the stock `bb-probe.exe` of each bbport release this runtime is built for.
    pub stock_probe_sha256: Vec<String>,
    /// SHA-256 of the patched `bb-probe.exe` this payload installs.
    pub probe_sha256: String,
    pub files: Vec<FileRef>,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
pub struct Manifest {
    pub format: u32,
    #[serde(default)] pub version: String,
    /// Files every install gets (the Mod Manager).
    #[serde(default)] pub shared: Vec<FileRef>,
    #[serde(default)] pub runtimes: Vec<RuntimeInfo>,
}

pub struct Payload<'a> { bytes: &'a [u8], pub manifest: Manifest }

/// What is installed in the port's `bin/bb-probe.exe`.
#[derive(Debug, PartialEq, Eq)]
pub enum Detection<'a> {
    /// An unmodified bbport release this payload has a runtime for.
    Stock(&'a RuntimeInfo),
    /// A runtime from this payload is already installed.
    Integrated(&'a RuntimeInfo),
    /// A bb-probe.exe this payload does not know (an unsupported bbport version, or an edited file).
    Unknown(String),
    /// No bb-probe.exe at all.
    Missing,
}

impl<'a> Payload<'a> {
    pub fn open(bytes: &'a [u8]) -> Result<Payload<'a>, String> {
        let mut a = zip::ZipArchive::new(io::Cursor::new(bytes)).map_err(|e| format!("The installer payload is damaged: {e}"))?;
        let manifest = match a.by_name("payload.json") {
            Ok(mut f) => {
                let mut text = String::new();
                f.read_to_string(&mut text).map_err(|e| format!("The installer payload is damaged: {e}"))?;
                serde_json::from_str::<Manifest>(&text).map_err(|e| format!("The installer payload manifest is unreadable: {e}"))?
            }
            // A build without a payload: nothing to install.
            Err(_) => Manifest { format: 2, version: String::new(), shared: vec![], runtimes: vec![] },
        };
        if manifest.format != 2 { return Err(format!("Unsupported payload format {}.", manifest.format)); }
        Ok(Payload { bytes, manifest })
    }

    pub fn is_empty(&self) -> bool { self.manifest.shared.is_empty() && self.manifest.runtimes.is_empty() }
    pub fn runtime(&self, id: &str) -> Option<&RuntimeInfo> { self.manifest.runtimes.iter().find(|r| r.id == id) }
    pub fn labels(&self) -> Vec<&str> { self.manifest.runtimes.iter().map(|r| r.label.as_str()).collect() }

    /// Identify the bbport release from the installed `bb-probe.exe`.
    pub fn detect(&self, probe: &Path) -> Detection<'_> {
        let Ok(sha) = sha_file(probe) else { return Detection::Missing };
        for r in &self.manifest.runtimes {
            if r.probe_sha256.eq_ignore_ascii_case(&sha) { return Detection::Integrated(r); }
        }
        for r in &self.manifest.runtimes {
            if r.stock_probe_sha256.iter().any(|h| h.eq_ignore_ascii_case(&sha)) { return Detection::Stock(r); }
        }
        Detection::Unknown(sha)
    }

    /// The files to install for `runtime` (the Mod Manager plus that runtime), checked against their hashes.
    pub fn entries(&self, runtime: Option<&RuntimeInfo>) -> Result<Vec<Entry>, String> {
        let mut a = zip::ZipArchive::new(io::Cursor::new(self.bytes)).map_err(|e| format!("The installer payload is damaged: {e}"))?;
        let mut out = Vec::new();
        let refs = self.manifest.shared.iter().chain(runtime.into_iter().flat_map(|r| r.files.iter()));
        for f in refs {
            let rel = rel_string(Path::new(&f.path))?;
            let mut blob = a.by_name(&format!("blobs/{}", f.sha256.to_ascii_lowercase()))
                .map_err(|_| format!("The installer payload is missing {}.", f.path))?;
            let mut data = Vec::with_capacity(blob.size() as usize);
            blob.read_to_end(&mut data).map_err(|e| format!("Cannot read {}: {e}", f.path))?;
            if !sha256_hex(&data).eq_ignore_ascii_case(&f.sha256) { return Err(format!("{} is damaged in the installer payload.", f.path)); }
            out.push(Entry { path: rel, data });
        }
        Ok(out)
    }
}

#[cfg(test)]
pub(crate) mod testing {
    use super::*;
    use std::io::Write;

    /// Build a payload zip: shared files plus runtimes of (id, stock probe bytes, patched files).
    pub fn build(shared: &[(&str, &[u8])], runtimes: &[(&str, &[u8], &[(&str, &[u8])])]) -> Vec<u8> {
        let mut blobs: Vec<(String, Vec<u8>)> = Vec::new();
        let mut add = |data: &[u8]| { let h = sha256_hex(data); if !blobs.iter().any(|(k, _)| *k == h) { blobs.push((h.clone(), data.to_vec())); } h };
        let fref = |p: &str, h: String| serde_json::json!({"path": p, "sha256": h});
        let shared_json: Vec<_> = shared.iter().map(|(p, d)| fref(p, add(d))).collect();
        let rt_json: Vec<_> = runtimes.iter().map(|(id, stock, files)| {
            let probe = files.iter().find(|(p, _)| *p == "bin/bb-probe.exe").map(|(_, d)| sha256_hex(d)).unwrap_or_default();
            let fs: Vec<_> = files.iter().map(|(p, d)| fref(p, add(d))).collect();
            serde_json::json!({"id": id, "label": format!("bbport {id}"), "stock_probe_sha256": [sha256_hex(stock)], "probe_sha256": probe, "files": fs})
        }).collect();
        let manifest = serde_json::json!({"format": 2, "version": "test", "shared": shared_json, "runtimes": rt_json});
        let mut w = zip::ZipWriter::new(io::Cursor::new(Vec::new()));
        let o = zip::write::SimpleFileOptions::default();
        w.start_file("payload.json", o).unwrap();
        w.write_all(manifest.to_string().as_bytes()).unwrap();
        for (h, d) in &blobs { w.start_file(format!("blobs/{h}"), o).unwrap(); w.write_all(d).unwrap(); }
        w.finish().unwrap().into_inner()
    }
}
