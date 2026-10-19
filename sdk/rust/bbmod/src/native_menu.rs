//! Declarative native menu definitions. ABI 1.8. Registrations are owned and
//! copied by the host. Capabilities distinguish registration from native rendering.
//! Open lists are frozen; changes appear when the native screen is rebuilt.
use crate::{check,sys,Cx,Error,Event,Id,Result};
use core::ffi::c_char;
pub const EVENTS:u32=sys::BBM_EV_NATIVE_MENU;
pub const MODS:&str="game.mods";
pub const SYSTEM:&str="game.system";
pub const CAP_REGISTRY:u32=1;
pub const CAP_LISTS:u32=2;
pub const CAP_SYSTEM:u32=4;
pub const CAP_INGAME:u32=8;
pub const CAP_OBSERVE:u32=16;
pub const CAP_SELECTION:u32=32;
#[repr(u32)]
#[derive(Debug,Clone,Copy,PartialEq,Eq)]
pub enum Kind { Screen=1, Action=2, Link=3, Toggle=4 }
#[derive(Debug,Clone)]
pub struct Node {
    pub key:String,pub parent:String,pub label:String,pub help:String,pub target:String,
    pub kind:Kind,pub enabled:bool,pub order:i32,pub value:bool,
}
impl Node {
    pub fn new(kind:Kind,key:&str,parent:&str,label:&str)->Self {
        Self{key:key.into(),parent:parent.into(),label:label.into(),help:String::new(),target:String::new(),kind,enabled:true,order:0,value:false}
    }
    fn raw(&self)->Result<sys::bbm_menu_node>{
        let mut out=sys::bbm_menu_node{size:core::mem::size_of::<sys::bbm_menu_node>() as u32,
            kind:self.kind as u32,flags:self.enabled as u32,order:self.order,value:self.value as i32,..Default::default()};
        copy(&self.key,&mut out.key)?;copy(&self.parent,&mut out.parent)?;copy(&self.label,&mut out.label)?;
        copy(&self.help,&mut out.help)?;copy(&self.target,&mut out.target)?;Ok(out)
    }
}
fn copy(s:&str,out:&mut[c_char])->Result<()> {
    if s.len()>=out.len()||s.as_bytes().contains(&0){return Err(Error::ARG)}
    for (a,b) in out.iter_mut().zip(s.bytes()){*a=b as c_char;}Ok(())
}
fn string(s:&[c_char])->Option<String>{
    let end=s.iter().position(|x|*x==0)?;
    String::from_utf8(s[..end].iter().map(|x|*x as u8).collect()).ok()
}
#[derive(Debug,Clone)]
pub struct State {pub capabilities:u32,pub revision:u64,pub generation:u64,pub screen:String}
impl State {pub fn supports(&self,flags:u32)->bool{self.capabilities&flags==flags}}
#[repr(u32)]
#[derive(Debug,Clone,Copy,PartialEq,Eq)]
pub enum Change {Opened=1,Closed=2,Activated=3,ValueChanged=4,Created=5,RegistryChanged=6,SelectionChanged=7}
#[derive(Debug,Clone)]
pub struct MenuEvent {
    pub change:Change,pub generation:u64,pub revision:u64,pub node:Id,pub value:bool,
    pub owner:String,pub key:String,pub screen:String,
    pub control:u64,pub selection:Option<(i32,i32)>,
}
impl Cx {
    pub fn native_menu_register(&mut self,node:&Node)->Result<Id>{
        let h=unsafe{&*self.host};if !h.has_native_menus(){return Err(Error::UNSUPPORTED)}
        let raw=node.raw()?;let mut id=0;check(unsafe{(h.menu_register)(self.me,&raw,&mut id)})?;Ok(Id(id))
    }
    pub fn native_menu_update(&mut self,id:Id,node:&Node)->Result<()>{
        let h=unsafe{&*self.host};if !h.has_native_menus(){return Err(Error::UNSUPPORTED)}
        check(unsafe{(h.menu_update)(self.me,id.0,&node.raw()?)})
    }
    pub fn native_menu_state(&self)->Result<State>{
        let h=unsafe{&*self.host};if !h.has_native_menus(){return Err(Error::UNSUPPORTED)}
        let mut raw=sys::bbm_menu_state::default();check(unsafe{(h.menu_state)(self.me,&mut raw)})?;
        if raw.size as usize!=core::mem::size_of_val(&raw){return Err(Error::STATE)}
        Ok(State{capabilities:raw.capabilities,revision:raw.revision,generation:raw.generation,screen:string(&raw.screen).ok_or(Error::STATE)?})
    }
}
impl Event<'_> {
    pub fn native_menu(&self)->Option<MenuEvent>{
        if self.kind!=EVENTS||self.data.len()!=core::mem::size_of::<sys::bbm_menu_event>(){return None}
        let raw=unsafe{core::ptr::read_unaligned(self.data.as_ptr().cast::<sys::bbm_menu_event>())};
        if raw.size as usize!=core::mem::size_of_val(&raw)||raw.reserved!=0||raw.flags!=0{return None}
        let change=match raw.kind{1=>Change::Opened,2=>Change::Closed,3=>Change::Activated,4=>Change::ValueChanged,5=>Change::Created,6=>Change::RegistryChanged,7=>Change::SelectionChanged,_=>return None};
        if change==Change::ValueChanged&&!(0..=1).contains(&raw.value){return None}
        if change==Change::SelectionChanged&&(raw.control==0||raw.value<0||raw.previous_value<0){return None}
        Some(MenuEvent{change,generation:raw.generation,revision:raw.revision,node:Id(raw.node),value:raw.value!=0,
            owner:string(&raw.owner)?,key:string(&raw.key)?,screen:string(&raw.screen)?,control:raw.control,
            selection:(change==Change::SelectionChanged).then_some((raw.previous_value,raw.value))})
    }
}
#[cfg(test)] mod tests {
    use super::*;
    #[test] fn layouts_and_strings(){
        assert_eq!(core::mem::size_of::<sys::bbm_menu_node>(),852);
        assert_eq!(core::mem::size_of::<sys::bbm_menu_state>(),216);
        assert_eq!(core::mem::size_of::<sys::bbm_menu_event>(),440);
        let mut n=Node::new(Kind::Action,"test",MODS,"こんにちは");assert!(n.raw().is_ok());
        n.key="x".repeat(64);assert_eq!(n.raw().err(),Some(Error::ARG));
        n.key="a\0b".into();assert_eq!(n.raw().err(),Some(Error::ARG));
    }
    #[test] fn malformed_events(){
        let raw=sys::bbm_menu_event{size:440,kind:4,value:7,..Default::default()};
        let bytes=unsafe{core::slice::from_raw_parts((&raw as *const sys::bbm_menu_event).cast::<u8>(),440)};
        assert!(Event{kind:EVENTS,a:0,b:0,data:bytes}.native_menu().is_none());
    }
}
