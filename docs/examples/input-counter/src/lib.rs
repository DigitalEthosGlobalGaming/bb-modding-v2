use bbmod::{native_menu::{self as menu,Kind,Node},Cx,Event,Id,Mod,PadChange,Result,Version};
const PAGE:&str="example.input-counter/settings";
struct Counter {presses:u64,row:Id}
fn value(presses:u64)->Node {
    let mut node=Node::new(Kind::Info,"count",PAGE,&format!("Button presses: {presses}"));
    node.help="Read-only shared input count. Survives mod reload; no game memory changes.".into();
    node
}
impl Mod for Counter {
    fn load(cx:&mut Cx)->Result<Self>{
        cx.subscribe(bbmod::sys::BBM_EV_PAD_CHANGED)?;
        let mut page=Node::new(Kind::Screen,"settings",menu::MODS,"Shared Input Counter");
        page.help="Inspect the shared controller press counter".into();
        cx.native_menu_register(&page)?;
        let row=cx.native_menu_register(&value(0))?;
        Ok(Self{presses:0,row})
    }
    fn event(&mut self,cx:&mut Cx,event:&Event){
        if let Some(change)=PadChange::from_event(event){
            self.presses=self.presses.saturating_add(change.pressed().count_ones() as u64);
            if let Err(error)=cx.native_menu_update(self.row,&value(self.presses)){
                cx.log_warn(&format!("Cannot refresh counter: {error}"));
            }
            bbmod::info!(cx,"input: total presses={}",self.presses);
        }
    }
    fn save_state(&self)->Option<Vec<u8>>{Some(self.presses.to_le_bytes().to_vec())}
    fn restore_state(&mut self,cx:&mut Cx,bytes:&[u8],_from:Version){
        if let Ok(bytes)=<[u8;8]>::try_from(bytes){
            self.presses=u64::from_le_bytes(bytes);
            if let Err(error)=cx.native_menu_update(self.row,&value(self.presses)){
                cx.log_warn(&format!("Cannot restore counter display: {error}"));
            }
        }
    }
}
bbmod::export!(Counter);
