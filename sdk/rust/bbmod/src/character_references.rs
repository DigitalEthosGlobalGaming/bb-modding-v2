//! Offline dictionaries. Team, raw script value, action state, action event and
//! action variable IDs are DISTINCT namespaces. No startup scans or JSON parsing.
#[derive(Debug,Clone,Copy,PartialEq,Eq,PartialOrd,Ord,Hash)]
pub struct TeamId(pub i32);
#[derive(Debug,Clone,Copy)]
pub struct TeamDefinition {pub id:TeamId,pub key:&'static str,pub name:&'static str}
#[derive(Debug,Clone,Copy,PartialEq,Eq,PartialOrd,Ord,Hash)]
pub struct ScriptValue(pub i32);
#[derive(Debug,Clone,Copy,PartialEq,Eq,Hash)]
pub struct ActionStateId(pub u32);
#[derive(Debug,Clone,Copy,PartialEq,Eq,Hash)]
pub struct ActionEventId(pub u32);
#[derive(Debug,Clone,Copy,PartialEq,Eq,Hash)]
pub struct ActionVariableId(pub u32);
#[derive(Debug,Clone,Copy)]
pub struct ActionName {pub id:u32,pub name:&'static str}
/// Lexical link only: e.g. raw 9000 resembles a state named Event9000. This
/// does NOT prove the actor is in that state or identify its AI goal/behavior.
#[derive(Debug,Clone,Copy)]
pub struct ScriptCandidate {pub value:ScriptValue,pub action_state:ActionStateId,pub name:&'static str}
pub const JSON: &str = include_str!(concat!(env!("OUT_DIR"), "/character-references.json"));
include!(concat!(env!("OUT_DIR"), "/character_references_generated.rs"));
pub fn get_team_definition(id:TeamId)->Option<&'static TeamDefinition>{TEAMS.binary_search_by_key(&id,|d|d.id).ok().map(|i|&TEAMS[i])}
pub fn team_by_key(key:&str)->Option<&'static TeamDefinition>{TEAMS.iter().find(|d|d.key==key)}
fn lookup(table:&'static [ActionName],id:u32)->Option<&'static ActionName>{table.binary_search_by_key(&id,|d|d.id).ok().map(|i|&table[i])}
pub fn action_state(id:ActionStateId)->Option<&'static ActionName>{lookup(STATES,id.0)}
pub fn action_event(id:ActionEventId)->Option<&'static ActionName>{lookup(EVENTS,id.0)}
pub fn action_variable(id:ActionVariableId)->Option<&'static ActionName>{lookup(VARIABLES,id.0)}
/// Research lead only, intentionally not named get_script_definition.
pub fn script_candidate(value:ScriptValue)->Option<&'static ScriptCandidate>{SCRIPT_CANDIDATES.binary_search_by_key(&value,|d|d.value).ok().map(|i|&SCRIPT_CANDIDATES[i])}
impl TeamId {pub fn definition(self)->Option<&'static TeamDefinition>{get_team_definition(self)}}
impl ScriptValue {pub fn candidate(self)->Option<&'static ScriptCandidate>{script_candidate(self)}}
#[cfg(test)] mod tests {use super::*;
#[test] fn namespaces_and_roundtrip() {for t in TEAMS {assert_eq!(team_by_key(t.key).unwrap().id,t.id);}for s in STATES {assert_eq!(action_state(ActionStateId(s.id)).unwrap().name,s.name);}for c in SCRIPT_CANDIDATES {assert_eq!(action_state(c.action_state).unwrap().name,c.name);}assert!(TeamId(999).definition().is_none());assert!(ScriptValue(-1).candidate().is_none());}
}
