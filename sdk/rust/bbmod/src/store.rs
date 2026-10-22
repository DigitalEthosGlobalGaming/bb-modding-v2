//! Persistent mod settings and data.
//!
//! Every mod can keep user-visible settings and private data across runs, in one
//! place beside the `bbmods` folder:
//!
//! ```text
//! <bbmods>/../config/<mod-id>.toml     settings (the file the host reads for cx.config)
//! <bbmods>/../mod-data/<mod-id>/...    private blobs
//! ```
//!
//! The settings file is the same user-override file the host already consults for
//! [`Cx::config`], so values written here are read back by `cx.config("section.key")`
//! on the next load with no extra code. It is plain TOML (`[section]`, `key = value`);
//! existing comments, unrelated keys and other sections are preserved when a value is
//! changed. Writes are atomic (temporary file, then rename) and debounced, so a slider
//! can be dragged without hammering the disk.
//!
//! This is pure `std`: it needs no host ABI, so it works with an installed runtime.
//! The mods directory comes from `BB_CODEMODS_DIR`, which the runtime sets, matching how
//! the host locates the same file.
//!
//! ```ignore
//! let mut store = bbmod::store::Store::open(cx)?;
//! let level: u32 = store.get_parse("difficulty", "level").unwrap_or(2);
//! store.set("difficulty", "level", 3);       // marks dirty
//! // every frame:
//! store.tick()?;                             // saves once the value has settled
//! ```
use crate::{Cx, Error, Result};
use std::fmt::Display;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::str::FromStr;
use std::time::{Duration, Instant, SystemTime};

/// Environment variable the runtime sets to the mods directory.
pub const ENV_MODS_DIR: &str = "BB_CODEMODS_DIR";
/// Settings stay unsaved this long after the last change, so drags coalesce.
pub const DEFAULT_DEBOUNCE: Duration = Duration::from_millis(1000);

/// A mod's settings file and private data folder.
pub struct Store {
    id: String,
    path: PathBuf,
    blob_dir: PathBuf,
    lines: Vec<String>,
    dirty: bool,
    dirty_since: Option<Instant>,
    debounce: Duration,
    /// Last content known to be on disk (read or written by us), and its mtime.
    disk: Option<String>,
    disk_mtime: Option<SystemTime>,
}

fn valid_name(s: &str) -> bool {
    !s.is_empty()
        && !s.starts_with('.')
        && s.len() <= 128
        && s.chars().all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '-'))
}

fn bad(msg: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidInput, msg.to_owned())
}

fn section_of(line: &str) -> Option<&str> {
    let t = line.trim();
    let inner = t.strip_prefix('[')?.strip_suffix(']')?;
    (!inner.starts_with('[')).then(|| inner.trim())
}

fn key_value(line: &str) -> Option<(&str, &str)> {
    let t = line.trim();
    if t.is_empty() || t.starts_with('#') || t.starts_with('[') {
        return None;
    }
    let (k, v) = t.split_once('=')?;
    Some((k.trim(), v.trim()))
}

/// The value text with quotes/escapes resolved, or with a trailing `# comment` removed.
fn unquote(raw: &str) -> String {
    let raw = raw.trim();
    let Some(rest) = raw.strip_prefix('"') else {
        return raw.split('#').next().unwrap_or("").trim().to_owned();
    };
    let mut out = String::new();
    let mut chars = rest.chars();
    while let Some(c) = chars.next() {
        match c {
            '"' => break,
            '\\' => match chars.next() {
                Some('n') => out.push('\n'),
                Some('t') => out.push('\t'),
                Some('r') => out.push('\r'),
                Some(o) => out.push(o),
                None => break,
            },
            o => out.push(o),
        }
    }
    out
}

fn quote(s: &str) -> String {
    let mut out = String::from('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\t' => out.push_str("\\t"),
            '\r' => out.push_str("\\r"),
            o => out.push(o),
        }
    }
    out.push('"');
    out
}

/// Index of the header line of `section` and the end (exclusive) of its body.
/// `""` is the top level, before any header.
fn section_span(lines: &[String], section: &str) -> Option<(usize, usize)> {
    let mut start = if section.is_empty() { Some(0) } else { None };
    for (i, l) in lines.iter().enumerate() {
        if let Some(name) = section_of(l) {
            if let Some(s) = start {
                return Some((s, i));
            }
            if name == section {
                start = Some(i + 1);
            }
        }
    }
    start.map(|s| (s, lines.len()))
}

fn locate(lines: &[String], section: &str, key: &str) -> Option<usize> {
    let (a, b) = section_span(lines, section)?;
    (a..b).find(|&i| key_value(&lines[i]).is_some_and(|(k, _)| k == key))
}

impl Store {
    /// Open the settings for the calling mod. The id comes from its `mod.toml`.
    pub fn open(cx: &Cx) -> Result<Store> {
        let id = cx.config("mod.id").ok_or(Error::NOT_FOUND)?;
        let dir = std::env::var_os(ENV_MODS_DIR).ok_or(Error::NOT_FOUND)?;
        Store::open_at(&Path::new(&dir).join(".."), &id).map_err(|_| Error::ARG)
    }

    /// Open under an explicit root (the folder that holds `config/` and `mod-data/`).
    /// A missing file is an empty store; an unreadable one is an error.
    pub fn open_at(root: &Path, id: &str) -> io::Result<Store> {
        if !valid_name(id) {
            return Err(bad("invalid mod id"));
        }
        let path = root.join("config").join(format!("{id}.toml"));
        let mut s = Store {
            id: id.to_owned(),
            blob_dir: root.join("mod-data").join(id),
            path,
            lines: Vec::new(),
            dirty: false,
            dirty_since: None,
            debounce: DEFAULT_DEBOUNCE,
            disk: None,
            disk_mtime: None,
        };
        s.reload()?;
        Ok(s)
    }

    pub fn id(&self) -> &str {
        &self.id
    }

    /// The settings file path (for display or for external tools).
    pub fn path(&self) -> &Path {
        &self.path
    }

    pub fn set_debounce(&mut self, d: Duration) {
        self.debounce = d;
    }

    fn reload(&mut self) -> io::Result<()> {
        match fs::read_to_string(&self.path) {
            Ok(text) => {
                self.lines = text.lines().map(str::to_owned).collect();
                self.disk_mtime = fs::metadata(&self.path).and_then(|m| m.modified()).ok();
                self.disk = Some(text);
            }
            Err(e) if e.kind() == io::ErrorKind::NotFound => {
                self.lines.clear();
                self.disk = None;
                self.disk_mtime = None;
            }
            Err(e) => return Err(e),
        }
        self.dirty = false;
        self.dirty_since = None;
        Ok(())
    }

    // ---- values ---------------------------------------------------------

    /// The value of `key` in `section` (`""` for top level), unquoted.
    pub fn get(&self, section: &str, key: &str) -> Option<String> {
        let i = locate(&self.lines, section, key)?;
        key_value(&self.lines[i]).map(|(_, v)| unquote(v))
    }

    pub fn get_parse<T: FromStr>(&self, section: &str, key: &str) -> Option<T> {
        self.get(section, key)?.trim().parse().ok()
    }

    pub fn get_bool(&self, section: &str, key: &str, default: bool) -> bool {
        match self.get(section, key).as_deref() {
            Some("true" | "1" | "on") => true,
            Some("false" | "0" | "off") => false,
            _ => default,
        }
    }

    /// Set a bare value (numbers, booleans). Use [`Store::set_str`] for text.
    pub fn set<T: Display>(&mut self, section: &str, key: &str, value: T) {
        self.set_raw(section, key, &value.to_string());
    }

    /// Set a text value, quoted and escaped.
    pub fn set_str(&mut self, section: &str, key: &str, value: &str) {
        self.set_raw(section, key, &quote(value));
    }

    fn set_raw(&mut self, section: &str, key: &str, raw: &str) {
        let key = key.trim();
        if key.is_empty() || key.contains(['=', '\n', '#']) || raw.contains('\n') || section.contains(['\n', '[', ']']) {
            return;
        }
        let line = format!("{key} = {raw}");
        if let Some(i) = locate(&self.lines, section, key) {
            // Keep a hand-written line if it already holds exactly this value.
            if key_value(&self.lines[i]).is_some_and(|(_, v)| unquote(v) == unquote(raw)) {
                return;
            }
            self.lines[i] = line;
        } else if let Some((a, b)) = section_span(&self.lines, section) {
            // After the last non-blank line of the section body (before the next header).
            let mut at = b;
            while at > a && self.lines[at - 1].trim().is_empty() {
                at -= 1;
            }
            self.lines.insert(at, line);
        } else {
            if self.lines.last().is_some_and(|l| !l.trim().is_empty()) {
                self.lines.push(String::new());
            }
            self.lines.push(format!("[{section}]"));
            self.lines.push(line);
        }
        self.mark_dirty();
    }

    pub fn remove(&mut self, section: &str, key: &str) {
        if let Some(i) = locate(&self.lines, section, key) {
            self.lines.remove(i);
            self.mark_dirty();
        }
    }

    fn mark_dirty(&mut self) {
        if !self.dirty {
            self.dirty = true;
        }
        self.dirty_since = Some(Instant::now());
    }

    // ---- saving ----------------------------------------------------------

    pub fn is_dirty(&self) -> bool {
        self.dirty
    }

    fn content(&self) -> String {
        if self.lines.is_empty() { String::new() } else { self.lines.join("\n") + "\n" }
    }

    /// Write now, atomically. Clears the dirty flag only on success.
    pub fn save(&mut self) -> io::Result<()> {
        write_atomic(&self.path, self.content().as_bytes())?;
        self.disk = Some(self.content());
        self.disk_mtime = fs::metadata(&self.path).and_then(|m| m.modified()).ok();
        self.dirty = false;
        self.dirty_since = None;
        Ok(())
    }

    /// Call every frame (or so). Saves once the last change is older than the
    /// debounce. Returns whether a save happened.
    pub fn tick(&mut self) -> io::Result<bool> {
        if self.dirty && self.dirty_since.is_some_and(|t| t.elapsed() >= self.debounce) {
            self.save()?;
            return Ok(true);
        }
        Ok(false)
    }

    /// Pick up edits made outside this process (a settings tool, a text editor, another
    /// UI). Does nothing while there are unsaved local changes. Returns true when the
    /// contents were replaced; re-read your values afterwards.
    pub fn poll_external(&mut self) -> io::Result<bool> {
        if self.dirty {
            return Ok(false);
        }
        let mtime = fs::metadata(&self.path).and_then(|m| m.modified()).ok();
        if mtime == self.disk_mtime {
            return Ok(false);
        }
        match fs::read_to_string(&self.path) {
            Ok(text) if self.disk.as_deref() == Some(text.as_str()) => {
                self.disk_mtime = mtime;
                Ok(false)
            }
            Ok(_) => {
                self.reload()?;
                Ok(true)
            }
            Err(e) if e.kind() == io::ErrorKind::NotFound => {
                let had = self.disk.is_some();
                self.reload()?;
                Ok(had)
            }
            Err(e) => Err(e),
        }
    }

    // ---- private blobs ---------------------------------------------------

    /// This mod's private data folder (created on first write).
    pub fn blob_dir(&self) -> &Path {
        &self.blob_dir
    }

    fn blob_path(&self, name: &str) -> io::Result<PathBuf> {
        if !valid_name(name) {
            return Err(bad("invalid blob name (use letters, digits, '.', '_' or '-')"));
        }
        Ok(self.blob_dir.join(name))
    }

    /// `Ok(None)` when it does not exist yet.
    pub fn read_blob(&self, name: &str) -> io::Result<Option<Vec<u8>>> {
        match fs::read(self.blob_path(name)?) {
            Ok(b) => Ok(Some(b)),
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(None),
            Err(e) => Err(e),
        }
    }

    pub fn write_blob(&self, name: &str, data: &[u8]) -> io::Result<()> {
        write_atomic(&self.blob_path(name)?, data)
    }

    pub fn remove_blob(&self, name: &str) -> io::Result<()> {
        match fs::remove_file(self.blob_path(name)?) {
            Err(e) if e.kind() != io::ErrorKind::NotFound => Err(e),
            _ => Ok(()),
        }
    }
}

impl Drop for Store {
    /// Best effort: pending changes are written when the mod unloads.
    fn drop(&mut self) {
        if self.dirty {
            let _ = self.save();
        }
    }
}

fn write_atomic(path: &Path, data: &[u8]) -> io::Result<()> {
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir)?;
    }
    let mut tmp = path.as_os_str().to_owned();
    tmp.push(".tmp");
    let tmp = PathBuf::from(tmp);
    fs::write(&tmp, data)?;
    fs::rename(&tmp, path).inspect_err(|_| {
        let _ = fs::remove_file(&tmp);
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU32, Ordering};

    fn temp_root() -> PathBuf {
        static N: AtomicU32 = AtomicU32::new(0);
        let p = std::env::temp_dir().join(format!("bbmod-store-{}-{}", std::process::id(), N.fetch_add(1, Ordering::SeqCst)));
        let _ = fs::remove_dir_all(&p);
        fs::create_dir_all(&p).unwrap();
        p
    }

    fn open(root: &Path) -> Store {
        let mut s = Store::open_at(root, "me.test").unwrap();
        s.set_debounce(Duration::ZERO);
        s
    }

    #[test]
    fn values_round_trip_through_disk() {
        let root = temp_root();
        let mut s = open(&root);
        s.set("tuning", "speed", 1.5);
        s.set("tuning", "enabled", true);
        s.set("", "top", 7);
        s.set_str("text", "msg", "say \"hi\" # not a comment\\ok");
        s.save().unwrap();
        assert!(!s.is_dirty());
        let t = open(&root);
        assert_eq!(t.get_parse::<f32>("tuning", "speed"), Some(1.5));
        assert!(t.get_bool("tuning", "enabled", false));
        assert_eq!(t.get_parse::<i32>("", "top"), Some(7));
        assert_eq!(t.get("text", "msg").unwrap(), "say \"hi\" # not a comment\\ok");
        assert_eq!(t.get("tuning", "missing"), None);
        assert!(t.get_bool("tuning", "missing", true));
        assert_eq!(s.path(), root.join("config").join("me.test.toml"));
    }

    #[test]
    fn written_file_is_host_readable_toml() {
        let root = temp_root();
        let mut s = open(&root);
        s.set("settings", "a", 1);
        s.set("settings", "b", false);
        s.save().unwrap();
        let text = fs::read_to_string(s.path()).unwrap();
        assert_eq!(text, "[settings]\na = 1\nb = false\n");
    }

    #[test]
    fn editing_preserves_comments_and_other_sections() {
        let root = temp_root();
        fs::create_dir_all(root.join("config")).unwrap();
        fs::write(
            root.join("config").join("me.test.toml"),
            "# my notes\ntop = 1\n\n[a]\nx = 1 # keep\ny = 2\n\n[b]\nz = 3\n",
        )
        .unwrap();
        let mut s = open(&root);
        s.set("a", "y", 20);
        s.set("a", "new", 5);
        s.set("c", "fresh", "v");
        s.set("", "top2", 9);
        s.save().unwrap();
        let text = fs::read_to_string(s.path()).unwrap();
        assert_eq!(text, "# my notes\ntop = 1\ntop2 = 9\n\n[a]\nx = 1 # keep\ny = 20\nnew = 5\n\n[b]\nz = 3\n\n[c]\nfresh = v\n");
        assert_eq!(s.get_parse::<i32>("a", "x"), Some(1), "comment stripped on read");
    }

    #[test]
    fn unchanged_values_do_not_dirty_or_rewrite() {
        let root = temp_root();
        fs::create_dir_all(root.join("config")).unwrap();
        fs::write(root.join("config").join("me.test.toml"), "[s]\nk = \"on\"  # note\n").unwrap();
        let mut s = open(&root);
        s.set_str("s", "k", "on");
        assert!(!s.is_dirty(), "same value keeps the hand-written line");
        s.set_str("s", "k", "off");
        assert!(s.is_dirty());
    }

    #[test]
    fn remove_and_invalid_keys() {
        let root = temp_root();
        let mut s = open(&root);
        s.set("s", "k", 1);
        s.remove("s", "k");
        assert_eq!(s.get("s", "k"), None);
        let before = s.content();
        s.set("s", "bad=key", 1);
        s.set("s", "", 1);
        s.set_str("s", "k", "line\nbreak");
        assert_eq!(s.get("s", "k").unwrap(), "line\nbreak", "newlines are escaped, not rejected");
        assert_ne!(s.content(), before);
        s.set("s]", "k", 1);
        assert!(!s.content().contains("[s]]"));
    }

    #[test]
    fn debounce_delays_the_save() {
        let root = temp_root();
        let mut s = Store::open_at(&root, "me.test").unwrap();
        s.set_debounce(Duration::from_secs(3600));
        s.set("s", "k", 1);
        assert!(!s.tick().unwrap());
        assert!(!s.path().exists());
        s.set_debounce(Duration::ZERO);
        assert!(s.tick().unwrap());
        assert!(s.path().exists());
        assert!(!s.tick().unwrap(), "clean after saving");
    }

    #[test]
    fn external_edits_are_picked_up_but_own_writes_are_not() {
        let root = temp_root();
        let mut s = open(&root);
        s.set("s", "k", 1);
        s.save().unwrap();
        assert!(!s.poll_external().unwrap(), "own write is not external");
        std::thread::sleep(Duration::from_millis(20));
        fs::write(s.path(), "[s]\nk = 2\n").unwrap();
        assert!(s.poll_external().unwrap());
        assert_eq!(s.get_parse::<i32>("s", "k"), Some(2));
        assert!(!s.poll_external().unwrap());
        s.set("s", "k", 3);
        std::thread::sleep(Duration::from_millis(20));
        fs::write(s.path(), "[s]\nk = 99\n").unwrap();
        assert!(!s.poll_external().unwrap(), "unsaved local changes win");
        assert_eq!(s.get_parse::<i32>("s", "k"), Some(3));
        fs::remove_file(s.path()).unwrap();
        s.save().unwrap();
    }

    #[test]
    fn deleted_file_resets_to_empty() {
        let root = temp_root();
        let mut s = open(&root);
        s.set("s", "k", 1);
        s.save().unwrap();
        fs::remove_file(s.path()).unwrap();
        assert!(s.poll_external().unwrap());
        assert_eq!(s.get("s", "k"), None);
    }

    #[test]
    fn dropping_flushes_pending_changes() {
        let root = temp_root();
        {
            let mut s = Store::open_at(&root, "me.test").unwrap();
            s.set_debounce(Duration::from_secs(3600));
            s.set("s", "k", 42);
        }
        assert_eq!(open(&root).get_parse::<i32>("s", "k"), Some(42));
    }

    #[test]
    fn blobs_round_trip_and_reject_bad_names() {
        let root = temp_root();
        let s = open(&root);
        assert_eq!(s.read_blob("save.bin").unwrap(), None);
        s.write_blob("save.bin", &[1, 2, 3]).unwrap();
        s.write_blob("save.bin", &[4, 5]).unwrap();
        assert_eq!(s.read_blob("save.bin").unwrap(), Some(vec![4, 5]));
        assert!(s.blob_dir().ends_with(Path::new("mod-data").join("me.test")));
        s.remove_blob("save.bin").unwrap();
        s.remove_blob("save.bin").unwrap();
        assert_eq!(s.read_blob("save.bin").unwrap(), None);
        for name in ["../x", "a/b", "a\\b", ".hidden", "", "x y"] {
            assert!(s.write_blob(name, b"x").is_err(), "{name:?} must be rejected");
            assert!(s.read_blob(name).is_err());
        }
    }

    #[test]
    fn bad_mod_ids_are_rejected() {
        let root = temp_root();
        for id in ["", "../evil", "a/b", ".x", "has space"] {
            assert!(Store::open_at(&root, id).is_err(), "{id:?}");
        }
    }

    #[test]
    fn crlf_files_are_read() {
        let root = temp_root();
        fs::create_dir_all(root.join("config")).unwrap();
        fs::write(root.join("config").join("me.test.toml"), "[s]\r\nk = 5\r\n").unwrap();
        assert_eq!(open(&root).get_parse::<i32>("s", "k"), Some(5));
    }
}
