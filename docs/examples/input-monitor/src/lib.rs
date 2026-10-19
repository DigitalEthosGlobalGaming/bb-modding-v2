use bbmod::{native_menu::{self as menu,Kind,Node},Cx,Event,Id,Mod,PadChange,Result};
const PAGE:&str="example.input-monitor/settings";
struct Monitor {row:Id}
fn value(buttons:u32)->Node {
    let mut node=Node::new(Kind::Info,"buttons",PAGE,&format!("Observed buttons: 0x{buttons:08x}"));
    node.help="Read-only controller state. Press/release details are recorded in bbmods.log.".into();
    node
}
impl Mod for Monitor {
    fn load(cx:&mut Cx)->Result<Self>{
        cx.subscribe(bbmod::sys::BBM_EV_PAD_CHANGED)?;
        let mut page=Node::new(Kind::Screen,"settings",menu::MODS,"Shared Input Monitor");
        page.help="Inspect the shared controller state".into();
        cx.native_menu_register(&page)?;
        let row=cx.native_menu_register(&value(0))?;
        Ok(Self{row})
    }
    fn event(&mut self,cx:&mut Cx,event:&Event){
        if let Some(change)=PadChange::from_event(event){
            if let Err(error)=cx.native_menu_update(self.row,&value(change.current)){
                cx.log_warn(&format!("Cannot refresh input state: {error}"));
            }
            bbmod::info!(cx,"input: current=0x{:08x} pressed=0x{:08x} released=0x{:08x}",
                change.current,change.pressed(),change.released());
        }
    }
}
bbmod::export!(Monitor);
