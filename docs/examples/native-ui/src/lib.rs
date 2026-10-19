use bbmod::{Cx,Event,Id,Mod,Result};
use bbmod::native_menu::{self as menu,Change,Kind,Node};
const SETTINGS:&str="example.native-ui/settings";
struct Example {dynamic:Option<Id>,logging:bool}
impl Mod for Example {
    fn load(cx:&mut Cx)->Result<Self>{
        cx.subscribe(menu::EVENTS)?;
        let mut screen=Node::new(Kind::Screen,"settings",menu::MODS,"Native Menu Example");
        screen.help="Native controls, dynamic contributions and menu events".into();
        cx.native_menu_register(&screen)?;
        let mut shortcut=Node::new(Kind::Link,"shortcut",menu::SYSTEM,"Native Menu Example");
        shortcut.target=SETTINGS.into();shortcut.help="Open the example's own native screen".into();
        cx.native_menu_register(&shortcut)?;
        let mut toggle=Node::new(Kind::Toggle,"logging",SETTINGS,"Log menu changes");
        toggle.value=true;toggle.help="Observe native menu creation and sampled open/close changes".into();
        cx.native_menu_register(&toggle)?;
        let mut dynamic=Node::new(Kind::Action,"dynamic",SETTINGS,"Add or remove an example row");
        dynamic.order=10;dynamic.help="Reopen this screen to see the changed list".into();
        cx.native_menu_register(&dynamic)?;
        let nested=Node::new(Kind::Screen,"details",SETTINGS,"Another native screen");
        cx.native_menu_register(&nested)?;
        let detail=Node::new(Kind::Action,"detail-action","example.native-ui/details","Log a native selection");
        cx.native_menu_register(&detail)?;
        let state=cx.native_menu_state()?;
        if !state.supports(menu::CAP_LISTS){cx.log_warn("Native menu definitions registered; the experimental game adapter is not enabled. No overlay fallback.");}
        else{cx.log_info("Native menu example registered: Mods and System; engine navigation still requires in-game validation.");}
        Ok(Self{dynamic:None,logging:true})
    }
    fn event(&mut self,cx:&mut Cx,event:&Event){
        let Some(ev)=event.native_menu() else{return};
        if self.logging{cx.log_info(&format!("native menu {:?}: screen={} owner={} key={} generation={} selection={:?}",ev.change,ev.screen,ev.owner,ev.key,ev.generation,ev.selection));}
        if ev.owner!="example.native-ui"{return}
        if ev.change==Change::ValueChanged&&ev.key=="logging"{self.logging=ev.value;}
        if ev.change==Change::Activated&&ev.key=="dynamic"{
            let result=if let Some(id)=self.dynamic {
                cx.release(id).map(|_|self.dynamic=None)
            }else{
                let mut node=Node::new(Kind::Action,"added",SETTINGS,"Dynamically added action");
                node.order=20;node.help="This contribution is removed independently on unload".into();
                cx.native_menu_register(&node).map(|id|self.dynamic=Some(id))
            };
            if let Err(e)=result{cx.log_warn(&format!("Dynamic menu change failed: {e}"));}
        }
    }
}
bbmod::export!(Example);
