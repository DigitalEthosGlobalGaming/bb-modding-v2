//! Mod lists, mod manifests and release lookup: pure data and URL logic, no I/O.
//!
//! A *source* is where a `modlist.json` comes from: a GitHub repository (`owner/repo` or its
//! URL, read from the repository's `modlist.json`) or a direct `https://` URL to a JSON file.
//! A list entry points at a mod's repository; the mod's own `mod.toml` (fetched from that
//! repository) is authoritative for its version and ABI. Binaries come from a GitHub Release.

use serde::Deserialize;

/// The list shown on a fresh install.
pub const OFFICIAL_SOURCE: &str = "DigitalEthosGlobalGaming/bb-modding-mod-list";
/// Highest `modlist.json` format version understood.
pub const LIST_VERSION: u32 = 1;
/// ABI this manager was built against; the loader is at least this new.
pub const HOST_ABI: (u32, u32) = (bbmod::sys::BBM_ABI_MAJOR as u32, bbmod::sys::BBM_ABI_MINOR as u32);

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct GitHubRepo { pub owner: String, pub name: String }

impl GitHubRepo {
    pub fn raw_url(&self, git_ref: &str, path: &str) -> String {
        format!("https://raw.githubusercontent.com/{}/{}/{}/{}", self.owner, self.name, git_ref, path.trim_start_matches('/'))
    }
    /// API URL of the release to install: the tagged release for a version-like ref, else the latest.
    pub fn release_url(&self, git_ref: Option<&str>) -> String {
        match git_ref.filter(|r| looks_like_tag(r)) {
            Some(tag) => format!("https://api.github.com/repos/{}/{}/releases/tags/{tag}", self.owner, self.name),
            None => format!("https://api.github.com/repos/{}/{}/releases/latest", self.owner, self.name),
        }
    }
}

fn looks_like_tag(r: &str) -> bool {
    let r = r.strip_prefix('v').unwrap_or(r);
    r.chars().next().is_some_and(|c| c.is_ascii_digit())
}

/// Parse `owner/repo`, `github.com/owner/repo` or `https://github.com/owner/repo[.git][/...]`.
pub fn parse_github_repo(s: &str) -> Option<GitHubRepo> {
    let s = s.trim().trim_end_matches('/');
    let s = s.strip_prefix("https://").unwrap_or(s);
    let s = s.strip_prefix("www.").unwrap_or(s);
    let s = s.strip_prefix("github.com/").unwrap_or(s);
    let mut parts = s.split('/');
    let owner = parts.next()?;
    let name = parts.next()?.trim_end_matches(".git");
    let valid = |p: &str| !p.is_empty() && p.len() <= 100 && p.chars().all(|c| c.is_ascii_alphanumeric() || matches!(c, '-' | '_' | '.'));
    if !valid(owner) || !valid(name) || owner.starts_with('.') || name.starts_with('.') { return None; }
    Some(GitHubRepo { owner: owner.into(), name: name.into() })
}

/// Where a mod list is fetched from.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Source {
    Repo(GitHubRepo),
    Url(String),
}

impl Source {
    pub fn parse(input: &str) -> Result<Source, String> {
        let t = input.trim();
        if t.is_empty() { return Err("Enter a GitHub repository or a link to a modlist.json.".into()); }
        if t.starts_with("http://") { return Err("Only https:// links are allowed.".into()); }
        if t.starts_with("https://") && !t.contains("github.com/") || t.ends_with(".json") {
            if !t.starts_with("https://") { return Err("A direct link must start with https://.".into()); }
            if t.len() > 512 || t.chars().any(|c| c.is_whitespace() || c.is_control()) { return Err("That link is not valid.".into()); }
            return Ok(Source::Url(t.to_owned()));
        }
        parse_github_repo(t).map(Source::Repo).ok_or_else(|| "Use owner/repo, a GitHub link, or an https:// link to a .json file.".to_string())
    }

    /// URL of the `modlist.json` document.
    pub fn list_url(&self) -> String {
        match self {
            Source::Repo(r) => r.raw_url("HEAD", "modlist.json"),
            Source::Url(u) => u.clone(),
        }
    }

    /// Stable text form, as saved in the source list.
    pub fn key(&self) -> String {
        match self { Source::Repo(r) => format!("{}/{}", r.owner, r.name), Source::Url(u) => u.clone() }
    }
}

#[derive(Clone, Debug, PartialEq, Deserialize)]
pub struct ListEntry {
    pub id: String,
    pub name: String,
    #[serde(default)] pub description: String,
    #[serde(default)] pub author: String,
    pub repo: String,
    #[serde(default, rename = "ref")] pub git_ref: Option<String>,
    #[serde(default)] pub abi: Option<String>,
    /// Release asset to install; by default the release's only/first `.zip`.
    #[serde(default)] pub release: Option<String>,
    /// Expected SHA-256 (hex) of the release asset; overrides a published `.sha256` file.
    #[serde(default)] pub sha256: Option<String>,
    /// Path of the manifest in the repository; `mod.toml` by default.
    #[serde(default)] pub mod_toml: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Deserialize)]
pub struct ModList {
    #[serde(default)] pub name: String,
    #[serde(default)] pub description: String,
    pub version: u32,
    pub mods: Vec<ListEntry>,
}

/// Valid mod ID: becomes a folder name under `bbmods/`.
pub fn valid_id(id: &str) -> bool {
    !id.is_empty() && id.len() <= 128 && !id.starts_with('.') && !id.contains("..")
        && id.chars().all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '-'))
}

impl ModList {
    pub fn parse(json: &str) -> Result<ModList, String> {
        let list: ModList = serde_json::from_str(json).map_err(|e| format!("Not a valid mod list: {e}"))?;
        if list.version == 0 || list.version > LIST_VERSION {
            return Err(format!("Mod list format {} is not supported (this manager reads up to {LIST_VERSION}).", list.version));
        }
        let mut seen = std::collections::HashSet::new();
        let mods: Vec<ListEntry> = list.mods.into_iter()
            .filter(|m| valid_id(&m.id) && !m.name.is_empty() && parse_github_repo(&m.repo).is_some() && seen.insert(m.id.clone()))
            .collect();
        Ok(ModList { mods, ..list })
    }
}

/// The parts of a mod's `mod.toml` the manager needs.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Manifest {
    pub id: String,
    pub name: String,
    pub version: (u16, u16, u16),
    pub abi: (u32, u32),
    pub dll: String,
    pub author: String,
    pub description: String,
    pub manage: bool,
}

#[derive(Deserialize)]
struct RawManifest { r#mod: RawMod }
#[derive(Deserialize)]
struct RawMod {
    id: String,
    #[serde(default)] name: String,
    #[serde(default)] version: String,
    #[serde(default)] abi: String,
    #[serde(default)] dll: String,
    #[serde(default)] author: String,
    #[serde(default)] description: String,
    #[serde(default)] manage: bool,
}

pub fn parse_version(s: &str) -> Option<(u16, u16, u16)> {
    let mut it = s.trim().trim_start_matches('v').split('.');
    let mut next = || -> Option<u16> { match it.next() { Some(p) => p.split(|c: char| !c.is_ascii_digit()).next()?.parse().ok(), None => Some(0) } };
    let v = (next()?, next()?, next()?);
    Some(v)
}

pub fn parse_abi(s: &str) -> Option<(u32, u32)> {
    let (a, b) = s.trim().split_once('.')?;
    Some((a.parse().ok()?, b.parse().ok()?))
}

impl Manifest {
    pub fn parse(text: &str) -> Result<Manifest, String> {
        let raw: RawManifest = toml::from_str(text).map_err(|e| format!("Bad mod.toml: {e}"))?;
        let m = raw.r#mod;
        if !valid_id(&m.id) { return Err(format!("Bad mod id \"{}\".", m.id)); }
        let abi = parse_abi(&m.abi).ok_or_else(|| "mod.toml has no valid abi (expected \"1.2\").".to_string())?;
        if !m.dll.is_empty() && (m.dll.contains(['/', '\\']) || m.dll.starts_with('.')) { return Err("The dll name must be a plain file name.".into()); }
        Ok(Manifest {
            name: if m.name.is_empty() { m.id.clone() } else { m.name },
            version: parse_version(&m.version).unwrap_or((0, 0, 0)),
            id: m.id, abi, dll: m.dll, author: m.author, description: m.description, manage: m.manage,
        })
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Compat { Compatible, NeedsNewerLoader, Unsupported }

/// A mod runs when its ABI major matches the loader's and its minor is not newer.
pub fn compat(mod_abi: (u32, u32), host: (u32, u32)) -> Compat {
    if mod_abi.0 != host.0 { Compat::Unsupported }
    else if mod_abi.1 > host.1 { Compat::NeedsNewerLoader }
    else { Compat::Compatible }
}

pub fn compat_text(c: Compat, mod_abi: (u32, u32)) -> String {
    match c {
        Compat::Compatible => format!("Compatible (ABI {}.{})", mod_abi.0, mod_abi.1),
        Compat::NeedsNewerLoader => format!("Needs a newer mod loader (ABI {}.{}, you have {}.{})", mod_abi.0, mod_abi.1, HOST_ABI.0, HOST_ABI.1),
        Compat::Unsupported => format!("Unsupported ABI {}.{}", mod_abi.0, mod_abi.1),
    }
}

#[derive(Clone, Debug, PartialEq, Deserialize)]
pub struct Asset { pub name: String, pub browser_download_url: String, #[serde(default)] pub size: u64 }

#[derive(Clone, Debug, PartialEq, Deserialize)]
pub struct Release { #[serde(default)] pub tag_name: String, #[serde(default)] pub assets: Vec<Asset> }

/// The asset to download and, if the release publishes one, its checksum file.
#[derive(Clone, Debug, PartialEq)]
pub struct Pick<'a> { pub zip: &'a Asset, pub checksum: Option<&'a Asset> }

pub fn pick_asset<'a>(release: &'a Release, wanted: Option<&str>, id: &str) -> Result<Pick<'a>, String> {
    let zips: Vec<&Asset> = release.assets.iter().filter(|a| a.name.to_ascii_lowercase().ends_with(".zip")).collect();
    let zip = match wanted {
        Some(w) => zips.iter().find(|a| a.name == w).copied().ok_or_else(|| format!("The release has no asset named {w}."))?,
        None => zips.iter().find(|a| a.name.contains(id)).copied().or_else(|| (zips.len() == 1).then(|| zips[0]))
            .ok_or_else(|| "The release has no single .zip to install.".to_string())?,
    };
    if !zip.browser_download_url.starts_with("https://") { return Err("The release asset is not an https link.".into()); }
    let sum_name = format!("{}.sha256", zip.name);
    let checksum = release.assets.iter().find(|a| a.name == sum_name && a.browser_download_url.starts_with("https://"));
    Ok(Pick { zip, checksum })
}

/// First hex token of a `.sha256` file (`<hash>` or `<hash>  <file>`).
pub fn parse_checksum(text: &str) -> Option<String> {
    let t = text.split_whitespace().next()?.to_ascii_lowercase();
    (t.len() == 64 && t.chars().all(|c| c.is_ascii_hexdigit())).then_some(t)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sources_accept_repos_urls_and_direct_json_only_over_https() {
        let official = Source::parse("DigitalEthosGlobalGaming/bb-modding-mod-list").unwrap();
        assert_eq!(official.list_url(), "https://raw.githubusercontent.com/DigitalEthosGlobalGaming/bb-modding-mod-list/HEAD/modlist.json");
        assert_eq!(Source::parse("https://github.com/a/b.git").unwrap(), Source::parse("a/b").unwrap());
        assert_eq!(Source::parse("https://example.com/lists/mods.json").unwrap().list_url(), "https://example.com/lists/mods.json");
        assert!(Source::parse("http://example.com/x.json").is_err());
        assert!(Source::parse("").is_err() && Source::parse("not a repo").is_err() && Source::parse("a/../b").is_err());
        assert_eq!(Source::parse(" a/b ").unwrap().key(), "a/b");
    }

    #[test]
    fn list_parsing_drops_bad_and_duplicate_entries() {
        let json = r#"{"version":1,"mods":[
            {"id":"a.one","name":"One","repo":"https://github.com/o/one","ref":"v1.0.0","abi":"1.1"},
            {"id":"a.one","name":"Dup","repo":"o/one"},
            {"id":"../evil","name":"Evil","repo":"o/evil"},
            {"id":"a.two","name":"Two","repo":"nonsense"}]}"#;
        let list = ModList::parse(json).unwrap();
        assert_eq!(list.mods.len(), 1);
        assert_eq!(list.mods[0].git_ref.as_deref(), Some("v1.0.0"));
        assert!(ModList::parse(r#"{"version":2,"mods":[]}"#).is_err());
        assert!(ModList::parse("nope").is_err());
    }

    #[test]
    fn manifest_parsing_and_compatibility() {
        let m = Manifest::parse("[mod]\nid=\"me.difficulty\"\nname=\"Difficulty\"\nversion=\"0.1.0\"\nabi=\"1.1\"\ndll=\"me_difficulty.dll\"\n").unwrap();
        assert_eq!((m.version, m.abi, m.manage), ((0, 1, 0), (1, 1), false));
        assert!(Manifest::parse("[mod]\nid=\"x\"\nabi=\"1.1\"\ndll=\"..\\\\x.dll\"\n").is_err());
        assert!(Manifest::parse("[mod]\nid=\"x\"\n").is_err());
        assert_eq!(compat((1, 1), (1, 11)), Compat::Compatible);
        assert_eq!(compat((1, 12), (1, 11)), Compat::NeedsNewerLoader);
        assert_eq!(compat((2, 0), (1, 11)), Compat::Unsupported);
        assert_eq!(parse_version("v1.2.3-beta"), Some((1, 2, 3)));
    }

    #[test]
    fn releases_pick_the_right_asset_and_checksum() {
        let repo = parse_github_repo("o/r").unwrap();
        assert!(repo.release_url(Some("v1.2.0")).ends_with("/releases/tags/v1.2.0"));
        assert!(repo.release_url(Some("main")).ends_with("/releases/latest") && repo.release_url(None).ends_with("/releases/latest"));
        let a = |n: &str| Asset { name: n.into(), browser_download_url: format!("https://x/{n}"), size: 1 };
        let rel = Release { tag_name: "v1".into(), assets: vec![a("notes.txt"), a("pkg.zip"), a("pkg.zip.sha256")] };
        let p = pick_asset(&rel, None, "me.pkg").unwrap();
        assert_eq!((p.zip.name.as_str(), p.checksum.map(|c| c.name.as_str())), ("pkg.zip", Some("pkg.zip.sha256")));
        let two = Release { tag_name: String::new(), assets: vec![a("a.zip"), a("b.zip")] };
        assert!(pick_asset(&two, None, "me.pkg").is_err());
        assert_eq!(pick_asset(&two, Some("b.zip"), "me.pkg").unwrap().zip.name, "b.zip");
        assert!(pick_asset(&two, Some("c.zip"), "x").is_err());
        let hex = "ab".repeat(32);
        assert_eq!(parse_checksum(&format!("{hex}  pkg.zip\n")), Some(hex));
        assert_eq!(parse_checksum("short"), None);
    }
}
