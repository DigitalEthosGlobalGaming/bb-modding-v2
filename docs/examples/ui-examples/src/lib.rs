use bbmod::{Cx, Event, Id, Mod, Result, Version};
use bbmod::native_menu::{self as menu,Change,Kind,Node,controls::{Choice,Integer,Text}};
use std::{cell::RefCell, rc::Rc};

#[derive(Clone, Debug, PartialEq)]
struct Values {
    dashboard_count: u64,
    settings_count: u64,
    enabled: bool,
    level: i32,
    style: usize,
    label: String,
    settings_visible: bool,
}
impl Default for Values {
    fn default() -> Self {
        Self { dashboard_count: 0, settings_count: 0, enabled: true, level: 50,
            style: 0, label: "Shared UI example".into(), settings_visible: true }
    }
}
impl Values {
    fn encode(&self) -> Vec<u8> {
        let mut bytes = b"UIEX\x01".to_vec();
        bytes.extend(self.dashboard_count.to_le_bytes());
        bytes.extend(self.settings_count.to_le_bytes());
        bytes.extend(self.level.to_le_bytes());
        bytes.extend([self.enabled as u8, self.style as u8, self.settings_visible as u8]);
        bytes.extend(self.label.as_bytes());
        bytes
    }
    fn decode(bytes: &[u8]) -> Option<Self> {
        if bytes.len() < 28 || bytes.len() > 155 || &bytes[..5] != b"UIEX\x01" { return None; }
        let level = i32::from_le_bytes(bytes[21..25].try_into().ok()?);
        if !(0..=100).contains(&level) || bytes[25] > 1 || bytes[26] > 2 || bytes[27] > 1 { return None; }
        let label=std::str::from_utf8(&bytes[28..]).ok()?;
        if label.chars().any(|c|c<' '||c=='\x7f'){return None;}
        Some(Self {
            dashboard_count: u64::from_le_bytes(bytes[5..13].try_into().ok()?),
            settings_count: u64::from_le_bytes(bytes[13..21].try_into().ok()?),
            level, enabled: bytes[25] != 0, style: bytes[26] as usize,
            settings_visible: bytes[27] != 0,
            label: label.to_owned(),
        })
    }
}
const OWNER:&str="example.ui-examples";
const DASHBOARD:&str="example.ui-examples/dashboard";
const SETTINGS:&str="example.ui-examples/settings";
fn clipped(s:&str,max:usize)->String {
    if s.len()<=max{return s.into()}
    let mut end=max-3;while !s.is_char_boundary(end){end-=1;}format!("{}...",&s[..end])
}
fn register_rows(cx:&mut Cx,nodes:Vec<Node>,rows:&mut Vec<(Id,Node)>)->Result<()> {
    for node in nodes {rows.push((cx.native_menu_register(&node)?,node));}Ok(())
}
fn sync_rows(cx:&mut Cx,rows:&mut[(Id,Node)],nodes:Vec<Node>)->Result<()> {
    for ((id,old),node) in rows.iter_mut().zip(nodes){
        if *old!=node{cx.native_menu_update(*id,&node)?;*old=node;}
    }Ok(())
}
fn dashboard(v:&Values,notice:&str)->Vec<Node> {
    let mut screen=Node::new(Kind::Screen,"dashboard",menu::MODS,"Shared UI Examples");
    screen.help="Native dashboard and removable settings. Values belong to this example only; reload preserves them.".into();
    let mut summary=Node::new(Kind::Info,"overview",DASHBOARD,&clipped(&v.label,127));
    if summary.label.is_empty(){summary.label="(Empty label)".into();}
    summary.help=format!("Enabled: {} | level {} | style {}. These controls change this example only; the native menu uses stock text styling.",v.enabled,v.level,["Neutral","Green","Orange"][v.style]);
    let mut increment=Node::new(Kind::Action,"dashboard-increment",DASHBOARD,"Increment");
    increment.help="Increment the dashboard counter. The settings screen has its own independent counter.".into();
    let count=Node::new(Kind::Info,"dashboard-count",DASHBOARD,&format!("Dashboard counter: {}",v.dashboard_count));
    let mut visible=Node::new(Kind::Toggle,"settings-visible",DASHBOARD,"Show example settings");
    visible.value=v.settings_visible;visible.help="Hide/show removes or restores only this mod's settings and cancels its pending text edit. Values are retained.".into();
    let mut reset=Node::new(Kind::Action,"reset",DASHBOARD,"Reset example counters");
    reset.help="Reset both counters without changing the other settings.".into();
    let mut status=Node::new(Kind::Info,"notice",DASHBOARD,if notice.is_empty(){"Ready"}else{notice});
    status.label=clipped(&status.label,127);status.help=clipped(notice,255);
    let mut nodes=vec![screen,summary,increment,count,visible,reset,status];
    for(i,n)in nodes.iter_mut().enumerate(){n.order=i as i32;}nodes
}
fn settings_rows(v:&Values)->Vec<Node> {
    let mut screen=Node::new(Kind::Screen,"settings",DASHBOARD,"Example settings");
    screen.order=4;screen.help="Changes apply to this example only. Back returns to its dashboard.".into();
    let mut enabled=Node::new(Kind::Toggle,"enabled",SETTINGS,"Enabled");enabled.value=v.enabled;
    enabled.help="Example boolean state; no game writes.".into();
    let mut increment=Node::new(Kind::Action,"settings-increment",SETTINGS,"Increment");
    increment.order=10;increment.help="Increment the settings counter, independently of the dashboard's identically labelled action.".into();
    let mut count=Node::new(Kind::Info,"settings-count",SETTINGS,&format!("Settings counter: {}",v.settings_count));count.order=11;
    vec![screen,enabled,increment,count]
}
#[derive(Default)]
struct Settings {rows:Vec<(Id,Node)>,level:Option<Integer>,style:Option<Choice>,label:Option<Text>}
impl Settings {
    fn register(cx:&mut Cx,v:&Values)->Result<Self>{
        let mut settings=Self::default();
        let result=(||{
            register_rows(cx,settings_rows(v),&mut settings.rows)?;
            settings.level=Some(Integer::register(cx,"example.ui-examples/level",SETTINGS,"Level",0,100,1,v.level)?);
            settings.style=Some(Choice::register(cx,"example.ui-examples/style",SETTINGS,"Style",&["Neutral","Green","Orange"],v.style)?);
            settings.label=Some(Text::register(cx,"example.ui-examples/label",SETTINGS,"Label",&v.label,127)?);
            Ok(())
        })();
        if let Err(error)=result{
            if let Err(rollback)=settings.release(cx){cx.log_warn(&format!("Settings registration rollback failed: {rollback}"));}
            return Err(error)
        }
        Ok(settings)
    }
    fn release(&mut self,cx:&Cx)->Result<()> {
        if let Some(c)=&mut self.label{c.release(cx)?;}self.label=None;
        if let Some(c)=&mut self.style{c.release(cx)?;}self.style=None;
        if let Some(c)=&mut self.level{c.release(cx)?;}self.level=None;
        while let Some((id,_))=self.rows.last(){cx.release(*id)?;self.rows.pop();}Ok(())
    }
    fn sync(&mut self,cx:&mut Cx,v:&Values)->Result<()> {
        sync_rows(cx,&mut self.rows,settings_rows(v))?;
        if let Some(c)=&mut self.level{if c.value()!=v.level{c.set(cx,v.level)?;}}
        if let Some(c)=&mut self.style{if c.value()!=v.style{c.set(cx,v.style)?;}}
        if let Some(c)=&mut self.label{if c.value()!=v.label{c.set(cx,&v.label)?;}}Ok(())
    }
}
#[derive(Default)]
struct State {values:Values,rows:Vec<(Id,Node)>,settings:Option<Settings>,notice:String}
impl State {
    fn refresh(&mut self,cx:&mut Cx)->Result<()> {
        if self.values.settings_visible&&self.settings.is_none(){
            match Settings::register(cx,&self.values){
                Ok(settings)=>{self.settings=Some(settings);self.notice="Settings registered".into();},
                Err(error)=>{self.values.settings_visible=false;self.notice=format!("Cannot register settings: {error}");}
            }
        }else if !self.values.settings_visible{
            if let Some(settings)=&mut self.settings{
                match settings.release(cx){
                    Ok(())=>{self.settings=None;self.notice="Only this mod's settings were removed; values retained".into();},
                    Err(error)=>{self.values.settings_visible=true;self.notice=format!("Cannot remove settings: {error}");}
                }
            }
        }
        if let Some(settings)=&mut self.settings{
            if let Some(label)=&mut settings.label{
                match label.poll(cx){Ok(Some(value))=>self.values.label=value,Ok(None)=>{},Err(error)=>self.notice=format!("Text edit failed: {error}")}
            }
            settings.sync(cx,&self.values)?;
        }
        sync_rows(cx,&mut self.rows,dashboard(&self.values,&self.notice))
    }
}
struct UiExamples {state:Rc<RefCell<State>>}
impl Mod for UiExamples {
    fn load(cx:&mut Cx)->Result<Self>{
        cx.subscribe(menu::EVENTS)?;
        let mut state=State::default();register_rows(cx,dashboard(&state.values,""),&mut state.rows)?;state.refresh(cx)?;
        let state=Rc::new(RefCell::new(state));let frame=state.clone();
        cx.on_frame_mut(move|cx|{if let Err(error)=frame.borrow_mut().refresh(cx){cx.log_warn(&format!("Cannot refresh UI example: {error}"));}})?;
        cx.log_info("UI example registered native dashboard and removable native settings; no game writes");Ok(Self{state})
    }
    fn event(&mut self,cx:&mut Cx,event:&Event){
        let Some(event)=event.native_menu()else{return};if event.owner!=OWNER{return}
        let mut state=self.state.borrow_mut();let s=&mut *state;
        if event.change==Change::ValueChanged{
            // The host has already applied the toggle optimistically. Track
            // that observed value before accepting it or rolling it back, or
            // our no-op cache would hide a failed registration's rollback.
            for(_,node)in &mut s.rows{if node.kind==Kind::Toggle&&node.key==event.key{node.value=event.value;}}
            if let Some(settings)=&mut s.settings{for(_,node)in &mut settings.rows{if node.kind==Kind::Toggle&&node.key==event.key{node.value=event.value;}}}
            match event.key.as_str(){"settings-visible"=>s.values.settings_visible=event.value,"enabled"=>s.values.enabled=event.value,_=>{}}
        }
        if event.change==Change::Activated{
            match event.key.as_str(){
                "dashboard-increment"=>s.values.dashboard_count=s.values.dashboard_count.saturating_add(1),
                "settings-increment"=>s.values.settings_count=s.values.settings_count.saturating_add(1),
                "reset"=>{s.values.dashboard_count=0;s.values.settings_count=0;},_=>{}
            }
            if let Some(settings)=&mut s.settings{
                if let Some(c)=&mut settings.level{match c.handle(cx,&event){Ok(Some(v))=>s.values.level=v,Ok(None)=>{},Err(e)=>s.notice=format!("Level change failed: {e}")}}
                if let Some(c)=&mut settings.style{match c.handle(cx,&event){Ok(Some(v))=>s.values.style=v,Ok(None)=>{},Err(e)=>s.notice=format!("Style change failed: {e}")}}
                if let Some(c)=&mut settings.label{if let Err(e)=c.handle(cx,&event){s.notice=format!("Cannot edit label: {e}");}}
            }
        }
    }
    fn save_state(&self)->Option<Vec<u8>>{Some(self.state.borrow().values.encode())}
    fn restore_state(&mut self,cx:&mut Cx,bytes:&[u8],_from:Version){
        if let Some(values)=Values::decode(bytes){
            let mut state=self.state.borrow_mut();state.values=values;
            if let Err(error)=state.refresh(cx){cx.log_warn(&format!("Cannot restore native example controls: {error}"));}
        }else{cx.log_warn("UI example ignored incompatible or corrupt saved state");}
    }
}
bbmod::export!(UiExamples);

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn restores_all_user_state_without_resource_handles() {
        let values = Values { dashboard_count: u64::MAX, settings_count: 7, enabled: false,
            level: 100, style: 2, label: "Unicode ✓".into(), settings_visible: false };
        assert_eq!(Values::decode(&values.encode()), Some(values));
    }
    #[test]
    fn rejects_old_short_invalid_and_oversized_state() {
        let valid = Values::default().encode();
        for n in 0..28 { assert_eq!(Values::decode(&valid[..n]), None); }
        for (offset, value) in [(4, 2), (25, 2), (26, 3), (27, 2), (28, 255), (28, 10)] {
            let mut corrupt = valid.clone(); corrupt[offset] = value;
            assert_eq!(Values::decode(&corrupt), None);
        }
        let mut oversized = valid; oversized.resize(156, b'a');
        assert_eq!(Values::decode(&oversized), None);
    }
}
