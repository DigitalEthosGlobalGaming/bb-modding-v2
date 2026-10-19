//! Native frame counter with the original twelve-byte hot-reload state format.
use bbmod::{native_menu::{self as menu,Kind,Node},Cx,Id,Mod,Result,Version};
use std::{cell::Cell,rc::Rc,time::{Duration,Instant}};
const PAGE:&str="example.hello-rust/settings";
fn display(frames:u64)->Node {
    let mut n=Node::new(Kind::Info,"frames",PAGE,&format!("Sync points seen: {frames}"));
    n.help="Read-only frame callback count. The display refreshes four times per second; the full count survives reload.".into();
    n
}
struct Hello {frames:Rc<Cell<u64>>,reloads:u32,row:Id}
impl Mod for Hello {
    fn load(cx:&mut Cx)->Result<Self>{
        let mut page=Node::new(Kind::Screen,"settings",menu::MODS,"Hello (Rust)");
        page.help="A native status screen whose counter survives hot reload".into();
        cx.native_menu_register(&page)?;
        let row=cx.native_menu_register(&display(0))?;
        let frames=Rc::new(Cell::new(0u64));let f=frames.clone();let mut next=Instant::now();
        cx.on_frame_mut(move|cx|{
            f.set(f.get().saturating_add(1));
            if Instant::now()>=next {
                next=Instant::now()+Duration::from_millis(250);
                if let Err(error)=cx.native_menu_update(row,&display(f.get())){cx.log_warn(&format!("Cannot refresh counter: {error}"));}
            }
        })?;
        cx.log_info("hello from Rust");Ok(Self{frames,reloads:0,row})
    }
    fn save_state(&self)->Option<Vec<u8>>{
        let mut v=self.frames.get().to_le_bytes().to_vec();v.extend(self.reloads.saturating_add(1).to_le_bytes());Some(v)
    }
    fn restore_state(&mut self,cx:&mut Cx,s:&[u8],_from:Version){
        if s.len()==12 {
            self.frames.set(u64::from_le_bytes(s[..8].try_into().unwrap()));
            self.reloads=u32::from_le_bytes(s[8..].try_into().unwrap());
            if let Err(error)=cx.native_menu_update(self.row,&display(self.frames.get())){cx.log_warn(&format!("Cannot restore counter: {error}"));}
        }
    }
}
bbmod::export!(Hello);
