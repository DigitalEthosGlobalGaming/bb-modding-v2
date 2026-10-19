//! Shared loaded-character observations. These are 250 ms samples, not native
//! spawn/death/damage/AI-decision callbacks. NPC class does not imply hostility.
use crate::{check,sys,Cx,Error,Event,Result};
pub use sys::{bbm_character as Character,bbm_character_summary as CharacterSummary};
pub use crate::character_definitions::{CharacterDefinition,NpcParamId,get_character_definition};
pub const AVAILABLE:u32=1;
pub const NPC_PARAM:u32=2;
pub const AI_MODULE:u32=4;
pub const SCRIPT_VALUE:u32=8;
pub const HEALTH:u32=16;
pub const CHANGED:u32=sys::BBM_EV_CHARACTERS_CHANGED;
#[derive(Debug,Clone)]
pub struct CharacterSnapshot {pub summary:CharacterSummary,pub entries:Vec<Character>}

#[derive(Debug,Clone,Copy,PartialEq,Eq,Hash)]
pub struct CharacterHandle(pub u32);
/// Reference to an observed lifetime. Re-resolve against a current snapshot;
/// this is neither a guest pointer nor authority for game writes.
#[derive(Debug,Clone,Copy,PartialEq,Eq,Hash)]
pub struct CharacterRef {epoch:u64,token:u64,handle:CharacterHandle}
impl CharacterRef {
    pub fn handle(self)->CharacterHandle{self.handle}
    pub fn token(self)->u64{self.token}
}
pub trait CharacterExt {
    fn definition(&self)->Option<&'static CharacterDefinition>;
    fn team(&self)->crate::character_references::TeamId;
    fn script(&self)->Option<crate::character_references::ScriptValue>;
}
impl CharacterExt for Character {
    fn team(&self)->crate::character_references::TeamId{crate::character_references::TeamId(self.team_type)}
    fn script(&self)->Option<crate::character_references::ScriptValue>{(self.flags&SCRIPT_VALUE!=0).then_some(crate::character_references::ScriptValue(self.script_value))}
    fn definition(&self)->Option<&'static CharacterDefinition>{
        if self.flags&NPC_PARAM==0||self.npc_param<0{return None;}
        get_character_definition(self.npc_param as u32)
    }
}
impl CharacterSnapshot {
    pub fn by_handle(&self,handle:CharacterHandle)->Option<&Character>{self.entries.iter().find(|c|c.handle==handle.0)}
    pub fn by_definition(&self,id:NpcParamId)->impl Iterator<Item=&Character>{
        self.entries.iter().filter(move|c|c.flags&NPC_PARAM!=0&&c.npc_param>=0&&c.npc_param as u32==id.0)
    }
    pub fn reference(&self,handle:CharacterHandle)->Option<CharacterRef>{
        let c=self.by_handle(handle)?;
        (self.summary.flags==1&&self.summary.epoch!=0&&c.token!=0).then_some(CharacterRef{epoch:self.summary.epoch,token:c.token,handle})
    }
    pub fn get(&self,reference:CharacterRef)->Option<&Character>{
        if self.summary.flags!=1||self.summary.epoch!=reference.epoch{return None;}
        self.by_handle(reference.handle).filter(|c|c.token==reference.token)
    }
}
fn valid_summary(s:&CharacterSummary)->bool{s.size as usize==core::mem::size_of::<CharacterSummary>()&&s.count<=512&&s.flags<=1}
fn unpack(p:sys::bbm_character_page,revision:u64,offset:u32,limit:u32)->Result<(CharacterSummary,Vec<Character>)>{
    if !valid_summary(&p.snapshot)||p.snapshot.flags!=1||p.snapshot.revision==0||p.snapshot.epoch==0||
        (revision!=0&&revision!=p.snapshot.revision)||p.offset!=offset||p.count>limit||p.count>32||
        offset>p.snapshot.count||p.count>p.snapshot.count-offset{return Err(Error::STATE);}
    let entries=p.entries[..p.count as usize].to_vec();
    if entries.iter().any(|e|e.size as usize!=core::mem::size_of::<Character>()||e.flags&AVAILABLE==0||e.token==0||e.kind>3||
        (e.flags&HEALTH!=0&&(e.max_health<=0||e.health<0||e.health>e.max_health))){return Err(Error::STATE);}
    Ok((p.snapshot,entries))
}
impl Cx {
    /// Copy the current sampled character for a reference, rejecting stale epochs
    /// or tokens. The shared cache can lag by 250 ms; this is read-only metadata.
    pub fn get_character(&self,reference:CharacterRef)->Result<Character>{
        self.characters()?.get(reference).copied().ok_or(Error::NOT_FOUND)
    }
    pub fn characters(&self)->Result<CharacterSnapshot>{
        let size=unsafe{core::ptr::addr_of!((*self.host).size).read()} as usize;
        if size<core::mem::offset_of!(sys::bbm_host,characters_page)+core::mem::size_of::<usize>(){return Err(Error::UNSUPPORTED);}
        let page=unsafe{core::ptr::addr_of!((*self.host).characters_page).read()};
        let mut result:Option<CharacterSnapshot>=None;
        loop {
            let (revision,offset)=result.as_ref().map(|s|(s.summary.revision,s.entries.len() as u32)).unwrap_or((0,0));
            let mut raw=sys::bbm_character_page::default();
            check(unsafe{page(self.me,revision,offset,32,&mut raw)})?;
            let (summary,entries)=unpack(raw,revision,offset,32)?;
            let s=result.get_or_insert_with(||CharacterSnapshot{summary,entries:Vec::new()});
            if s.summary!=summary||(entries.is_empty()&&offset<summary.count){return Err(Error::STATE);}
            for e in entries {if s.entries.iter().any(|p|p.token==e.token||p.handle==e.handle){return Err(Error::STATE);}s.entries.push(e);}
            if s.entries.len()==summary.count as usize{return Ok(result.unwrap());}
        }
    }
}
impl Event<'_>{
    pub fn characters_changed(&self)->Option<CharacterSummary>{
        if self.kind!=CHANGED||self.data.len()!=core::mem::size_of::<CharacterSummary>(){return None;}
        let s=unsafe{core::ptr::read_unaligned(self.data.as_ptr().cast::<CharacterSummary>())};
        valid_summary(&s).then_some(s)
    }
}
#[derive(Debug,Clone,Copy,PartialEq,Eq)]
pub enum Observation {
    /// New baseline (first sample, load, unavailable data or sampling gap).
    Reset,
    /// First seen in this valid sample sequence; not necessarily a spawn.
    Appeared(Character),
    /// No longer in a complete valid sample; not necessarily dead.
    Disappeared(Character),
    Changed{before:Character,after:Character},
}
/// Per-mod comparison state over the host's shared cache. Call reset on an
/// unavailable summary/error. Never infer a death or encounter win from absence.
#[derive(Default)]
pub struct CharacterTracker {previous:Option<CharacterSnapshot>}
impl CharacterTracker {
    pub fn reset(&mut self){self.previous=None;}
    pub fn update(&mut self,next:CharacterSnapshot)->Vec<Observation>{
        let mut events=Vec::new();
        if let Some(old)=&self.previous {
            if old.summary.epoch==next.summary.epoch {
                for p in &old.entries {if !next.entries.iter().any(|n|n.token==p.token){events.push(Observation::Disappeared(*p));}}
                for n in &next.entries {
                    match old.entries.iter().find(|p|p.token==n.token) {
                        Some(p) if p!=n=>events.push(Observation::Changed{before:*p,after:*n}),
                        None=>events.push(Observation::Appeared(*n)),_=>{}
                    }
                }
            }else{events.push(Observation::Reset);}
        }else{events.push(Observation::Reset);}
        self.previous=Some(next);events
    }
}
/// A label for known NPC name IDs, NOT detection of an active boss encounter.
/// Variants, phases, doubles and chalice actors can share a label; list incomplete.
pub fn boss_archetype(c:&Character)->Option<&'static str>{
    if c.kind!=3||c.flags&NPC_PARAM==0{return None;}
    Some(match c.name_id {
        500000=>"Cleric Beast",271000=>"Father Gascoigne",209000=>"Blood-starved Beast",
        502000=>"Vicar Amelia",210000=>"Witch of Hemwick",508000=>"Darkbeast Paarl",
        212010=>"Shadow of Yharnam",510000=>"Rom",507000=>"The One Reborn",232000=>"Martyr Logarius",
        512000=>"Amygdala",257000=>"Celestial Emissary",251000=>"Ebrietas",551000=>"Mergo's Wet Nurse",
        451000=>"Ludwig",403000=>"Living Failures",452000=>"Lady Maria",450000=>"Laurence",453000=>"Orphan of Kos",
        _=>return None
    })
}
#[cfg(test)]mod tests{
    use super::*;
    fn sample(epoch:u64,token:u64,hp:i32)->CharacterSnapshot{CharacterSnapshot{summary:CharacterSummary{size:32,flags:1,revision:1,epoch,count:1,reserved:0},entries:vec![Character{size:56,flags:AVAILABLE|HEALTH,token,health:hp,max_health:100,..Default::default()}]}}
    #[test]fn layouts_and_malformed(){
        assert_eq!(core::mem::size_of::<Character>(),56);assert_eq!(core::mem::size_of::<sys::bbm_character_page>(),1832);
        assert!(unpack(Default::default(),0,0,32).is_err());
        let mut p=sys::bbm_character_page::default();let s=sample(1,1,50);p.snapshot=s.summary;p.count=1;p.entries[0]=s.entries[0];
        assert!(unpack(p,0,0,32).is_ok());p.entries[0].health=101;assert!(unpack(p,0,0,32).is_err());
        p.entries[0].health=50;p.count=33;assert!(unpack(p,0,0,32).is_err());
        assert!(Event{kind:CHANGED,a:0,b:0,data:&[]}.characters_changed().is_none());
    }
    #[test]fn lifetime_and_epoch_boundaries(){
        let mut t=CharacterTracker::default();assert_eq!(t.update(sample(1,1,100)),vec![Observation::Reset]);
        assert!(matches!(t.update(sample(1,1,0))[0],Observation::Changed{..}));
        let replaced=t.update(sample(1,2,100));assert!(matches!(replaced[0],Observation::Disappeared(_)));assert!(matches!(replaced[1],Observation::Appeared(_)));
        assert_eq!(t.update(sample(2,3,10)),vec![Observation::Reset]);
        t.reset();assert_eq!(t.update(sample(2,3,0)),vec![Observation::Reset]);
    }
    #[test]fn boss_label_requires_npc_and_parameter(){
        let mut c=Character{kind:3,name_id:500000,..Default::default()};assert!(boss_archetype(&c).is_none());
        c.flags=NPC_PARAM;assert_eq!(boss_archetype(&c),Some("Cleric Beast"));c.kind=2;assert!(boss_archetype(&c).is_none());
    }
    #[test]fn old_host_without_appended_function(){
        let header=[8usize];let cx=Cx::new(header.as_ptr().cast(),core::ptr::null_mut());
        assert!(matches!(cx.characters(),Err(Error::UNSUPPORTED)));
    }
    #[test]fn references_reject_replaced_instances_and_epochs(){
        let mut s=sample(1,2,100);s.entries[0].handle=123;s.entries[0].npc_param=500000;s.entries[0].flags|=NPC_PARAM;
        let r=s.reference(CharacterHandle(123)).unwrap();assert!(s.get(r).is_some());
        assert_eq!(s.by_definition(NpcParamId(500000)).count(),1);if let Some(d)=s.entries[0].definition(){assert_eq!(d.id,NpcParamId(500000));}
        s.entries[0].token=3;assert!(s.get(r).is_none());s.entries[0].token=2;s.summary.epoch=2;assert!(s.get(r).is_none());
        s.summary.epoch=1;s.entries.clear();assert!(s.get(r).is_none());
    }
}
