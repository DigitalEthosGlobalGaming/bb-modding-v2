//! Native toggles for owned, reversible gameplay patches. Hotkeys are preserved.
use bbmod::{native_menu::{self as menu,Change,Kind,Node},key,Cx,Event,Id,Mod,Result,Version};
use std::{cell::RefCell,rc::Rc};
const PAGE:&str="example.live-patches/settings";
/// (XML metadata name, Ctrl hotkey, player-facing label).
const ENTRIES:&[(&str,i32,&str)]=&[
    ("Player No Dead (Read note)",key::F1,"Can't die"),
    ("Player Stealth (Read note)",key::F2,"Enemies ignore you"),
    ("Player Silent (Read note)",key::F3,"Silent"),
    ("No Rally Decay",key::F4,"Rally never decays"),
    ("Disable Rally (HP Regain)",key::F5,"No rally"),
    ("Increased camera distance",key::F6,"Camera further out"),
    ("Disable Camera Auto Rotation via Movement",key::F7,"No camera auto-turn"),
    ("DS1-like physics",key::F9,"DS1 ragdolls"),
];
#[derive(Default)]
struct Entry {id:Option<Id>,error:Option<String>,menu:Option<Id>}
struct Patches {xml:String,entries:Vec<Entry>,notice:String,status:Option<Id>}
fn bounded(s:&str,max:usize)->String {
    let mut out=String::new();for c in s.chars(){let c=if c.is_control(){' '}else{c};if out.len()+c.len_utf8()>max{break;}out.push(c);}out
}
impl Patches {
    fn is_on(&self,i:usize)->bool{self.entries[i].id.is_some()}
    fn node(&self,i:usize)->Node {
        let mut n=Node::new(Kind::Toggle,&format!("patch-{i}"),PAGE,ENTRIES[i].2);n.order=i as i32;n.value=self.is_on(i);
        n.help=bounded(&self.entries[i].error.as_ref().map(|e|format!("Failed: {e}. Full details: bbmods.log")).unwrap_or_else(||format!("{}. Off/unload restores this mod's original bytes. Launch-time patches remain separate.",ENTRIES[i].0)),255);n
    }
    fn status_node(&self)->Node {
        let mut n=Node::new(Kind::Info,"status",PAGE,&bounded(&self.notice,127));n.order=100;
        n.help="Ctrl+F1..F9 hotkeys remain available. Errors are logged in full to bbmods.log.".into();n
    }
    fn sync(&self,cx:&mut Cx){
        for (i,e) in self.entries.iter().enumerate(){if let Some(id)=e.menu{if let Err(error)=cx.native_menu_update(id,&self.node(i)){cx.log_warn(&format!("Cannot update patch row: {error}"));}}}
        if let Some(id)=self.status{if let Err(error)=cx.native_menu_update(id,&self.status_node()){cx.log_warn(&format!("Cannot update patch status: {error}"));}}
    }
    fn set(&mut self,cx:&Cx,i:usize,on:bool){
        if on==self.is_on(i){return;}
        let e=&mut self.entries[i];
        if on {match cx.patch_xml(&self.xml,Some(ENTRIES[i].0)){
            Ok(id)=>{e.id=Some(id);e.error=None;},
            Err(error)=>e.error=Some(format!("{error}: {}",cx.last_error())),
        }} else if let Some(id)=e.id {match cx.release(id){
            Ok(())=>{e.id=None;e.error=None;},Err(error)=>e.error=Some(format!("{error}: {}",cx.last_error())),
        }}
        self.notice=if let Some(error)=&e.error {cx.log_warn(&format!("{}: {error}",ENTRIES[i].0));format!("Failed: {} (see row help)",ENTRIES[i].2)}
            else {format!("{}: {}",ENTRIES[i].2,if e.id.is_some(){"On"}else{"Off"})};
    }
}
struct LivePatches {patches:Rc<RefCell<Patches>>}
impl Mod for LivePatches {
    fn load(cx:&mut Cx)->Result<Self>{
        cx.subscribe(menu::EVENTS)?;
        let mut page=Node::new(Kind::Screen,"settings",menu::MODS,"Live Patches");
        page.help="Toggle owned gameplay patches; unload restores their original bytes".into();cx.native_menu_register(&page)?;
        let mut p=Patches{xml:cx.config("patches.xml").unwrap_or_else(||"Bloodborne.xml".into()),entries:ENTRIES.iter().map(|_|Entry::default()).collect(),notice:"Select a patch, or use Ctrl+F1..F9".into(),status:None};
        for i in 0..ENTRIES.len(){p.entries[i].menu=Some(cx.native_menu_register(&p.node(i))?);}
        p.status=Some(cx.native_menu_register(&p.status_node())?);
        let patches=Rc::new(RefCell::new(p));let frame=patches.clone();
        cx.on_frame_mut(move|cx|{
            if !(cx.key_down(key::LCTRL)||cx.key_down(key::RCTRL)){return;}
            for (i,(_,key,label)) in ENTRIES.iter().enumerate(){if cx.key_pressed(*key){let mut p=frame.borrow_mut();let on=!p.is_on(i);p.set(cx,i,on);p.sync(cx);cx.notify(&format!("{label}: {}",if p.is_on(i){"on"}else{"off"}));}}
        })?;
        Ok(Self{patches})
    }
    fn event(&mut self,cx:&mut Cx,event:&Event){
        let Some(ev)=event.native_menu() else{return;};
        if ev.owner!="example.live-patches"||ev.change!=Change::ValueChanged{return;}
        let Some(i)=ev.key.strip_prefix("patch-").and_then(|s|s.parse::<usize>().ok()).filter(|i|*i<ENTRIES.len())else{return;};
        let mut p=self.patches.borrow_mut();p.set(cx,i,ev.value);p.sync(cx);
    }
    fn save_state(&self)->Option<Vec<u8>>{let p=self.patches.borrow();Some((0..ENTRIES.len()).map(|i|p.is_on(i) as u8).collect())}
    fn restore_state(&mut self,cx:&mut Cx,s:&[u8],_from:Version){let mut p=self.patches.borrow_mut();for(i,&on)in s.iter().enumerate().take(ENTRIES.len()){if on!=0{p.set(cx,i,true);}}p.sync(cx);}
}
bbmod::export!(LivePatches);
