// SDK and generated lookups are MIT OR Apache-2.0; local game data is not distributed.
use std::{env,fs,path::PathBuf};
fn main() {
    let out=PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let local=PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap()).join("local-data");
    println!("cargo:rerun-if-changed=local-data");
    let enabled=env::var_os("CARGO_FEATURE_LOCAL_CATALOGS").is_some();
    let fallback=[
        ("items_generated.rs","pub static ITEMS: &[Item] = &[]; static ID_INDEX: &[usize] = &[]; pub mod ids {}"),
        ("character_definitions_generated.rs","pub static DEFINITIONS: &[CharacterDefinition] = &[]; static NAME_INDEX: &[usize] = &[];"),
        ("character_references_generated.rs","pub static TEAMS: &[TeamDefinition] = &[]; pub static STATES: &[ActionName] = &[]; pub static EVENTS: &[ActionName] = &[]; pub static VARIABLES: &[ActionName] = &[]; pub static SCRIPT_CANDIDATES: &[ScriptCandidate] = &[];"),
        ("items.json","{\"items\":{},\"available\":false}"),
        ("characters.json","{\"characters\":[],\"available\":false}"),
        ("character-references.json","{\"teams\":[],\"available\":false}")
    ];
    for (name,empty) in fallback {
        if enabled {
            fs::copy(local.join(name),out.join(name)).unwrap_or_else(|e|panic!("Generate local catalogs with setup.ps1 before enabling local-catalogs: {name}: {e}"));
        } else {fs::write(out.join(name),empty).unwrap();}
    }
}
