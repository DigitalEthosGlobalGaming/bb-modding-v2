//! Reusable native controls. Call `handle` from `Mod::event` after subscribing
//! to native menu events. Paths are `your.mod.id/key`; the parent may be any
//! registered screen. Each control opens a native child screen and Back returns
//! normally. Open-screen feedback requires CAP_LIVE. Resources belong to Cx and
//! are released by the host on unload; use `release` to remove one earlier.
use super::{Change,Kind,MenuEvent,Node};
use crate::{Cx,Error,Id,Result};

struct Rows(Vec<(Id,Node)>);
impl Rows {
    fn register(cx:&mut Cx,nodes:Vec<Node>)->Result<Self>{
        for n in &nodes { n.raw()?; }
        let mut rows=Vec::new();
        for node in nodes {
            match cx.native_menu_register(&node) {
                Ok(id)=>rows.push((id,node)),
                Err(error)=>{for (id,_) in rows.into_iter().rev(){let _=cx.release(id);}return Err(error)}
            }
        }
        Ok(Self(rows))
    }
    fn apply(&mut self,cx:&mut Cx,nodes:Vec<Node>)->Result<()> {
        if nodes.len()!=self.0.len(){return Err(Error::STATE)}
        for n in &nodes {n.raw()?;}
        let mut changed=Vec::new();
        for (index,((id,old),node)) in self.0.iter().zip(&nodes).enumerate(){
            if old==node {continue}
            if let Err(error)=cx.native_menu_update(*id,node){
                for prior in changed.into_iter().rev(){
                    let (id,old)=&self.0[prior];
                    if let Err(rollback)=cx.native_menu_update(*id,old){cx.log_warn(&format!("Native control rollback failed: {rollback}"));}
                }
                return Err(error)
            }
            changed.push(index);
        }
        for ((_,old),new) in self.0.iter_mut().zip(nodes){*old=new;}
        Ok(())
    }
    fn release(&mut self,cx:&Cx)->Result<()> {
        while let Some((id,_))=self.0.last(){cx.release(*id)?;self.0.pop();}
        Ok(())
    }
    fn activated(&self,event:&MenuEvent)->Option<usize>{
        if event.change!=Change::Activated{return None}
        self.0.iter().position(|(id,_)|*id==event.node)
    }
}
fn screen(path:&str,parent:&str,label:&str)->Result<Node>{
    let (owner,key)=path.split_once('/').ok_or(Error::ARG)?;
    if owner.is_empty()||key.is_empty()||key.contains('/') {return Err(Error::ARG)}
    Ok(Node::new(Kind::Screen,key,parent,label))
}

/// Explicit alternatives, including the accepted value and selection marker.
pub struct Choice {
    rows:Rows,path:String,parent:String,label:String,choices:Vec<String>,value:usize,
}
impl Choice {
    pub fn register(cx:&mut Cx,path:&str,parent:&str,label:&str,choices:&[&str],value:usize)->Result<Self>{
        if choices.is_empty()||choices.len()>32||value>=choices.len(){return Err(Error::ARG)}
        let mut control=Self{rows:Rows(Vec::new()),path:path.into(),parent:parent.into(),label:label.into(),choices:choices.iter().map(|s|(*s).into()).collect(),value};
        control.rows=Rows::register(cx,control.nodes(value)?)?;Ok(control)
    }
    fn nodes(&self,value:usize)->Result<Vec<Node>>{
        let mut root=screen(&self.path,&self.parent,&format!("{}: {}",self.label,self.choices[value]))?;
        root.help="Open to see all alternatives. Selecting one applies it immediately; Back returns to the previous menu.".into();
        let prefix=root.key.clone();let mut nodes=vec![root];
        for (i,label) in self.choices.iter().enumerate(){
            let mut node=Node::new(Kind::Action,&format!("{prefix}-{i}"),&self.path,&format!("{}{}",if i==value{"Selected: "}else{""},label));
            node.order=i as i32;node.help=format!("Choose {label}. Applies immediately; Back returns to the previous menu.");nodes.push(node);
        }
        Ok(nodes)
    }
    pub fn value(&self)->usize{self.value}
    pub fn set(&mut self,cx:&mut Cx,value:usize)->Result<()> {
        if value>=self.choices.len(){return Err(Error::ARG)}
        self.rows.apply(cx,self.nodes(value)?)?;self.value=value;Ok(())
    }
    pub fn handle(&mut self,cx:&mut Cx,event:&MenuEvent)->Result<Option<usize>>{
        let Some(index)=self.rows.activated(event) else{return Ok(None)};
        if index==0{return Ok(None)}
        self.set(cx,index-1)?;Ok(Some(self.value))
    }
    pub fn release(&mut self,cx:&Cx)->Result<()>{self.rows.release(cx)}
}

/// Bounded integer with fine/coarse adjustments. Arithmetic uses i64 so the
/// full i32 range is safe. At a bound, the unavailable direction is disabled.
pub struct Integer {
    rows:Rows,path:String,parent:String,label:String,min:i32,max:i32,step:i32,value:i32,
}

/// Native action that edits its full value through the shared system dialog.
/// Call `handle` on menu events and `poll` at host sync points. Captions are
/// shortened at UTF-8 boundaries; the full value (up to 4095 bytes) is retained.
pub struct Text {
    rows:Rows,label:String,value:String,max_bytes:usize,
    pending:Option<crate::text_entry::TextEntry>,
}
fn caption(s:&str,max:usize)->String{
    if s.len()<=max{return s.into()}
    let mut end=max.saturating_sub(3);while !s.is_char_boundary(end){end-=1;}
    format!("{}...",&s[..end])
}
impl Text {
    pub fn register(cx:&mut Cx,path:&str,parent:&str,label:&str,initial:&str,max_bytes:usize)->Result<Self>{
        if label.is_empty()||label.len()>127||!(1..=4095).contains(&max_bytes)||initial.len()>max_bytes||initial.chars().any(|c|c<' '||c=='\x7f'){return Err(Error::ARG)}
        let mut node=screen(path,parent,label)?;node.kind=Kind::Action;
        node.label=caption(&format!("{label}: {initial}"),127);
        node.help="Select to edit using the system text dialog. Enter/Cross accepts; Escape/Circle cancels. Requires a keyboard to type.".into();
        Ok(Self{rows:Rows::register(cx,vec![node])?,label:label.into(),value:initial.into(),max_bytes,pending:None})
    }
    fn display(&mut self,cx:&mut Cx,value:&str,pending:bool,notice:&str)->Result<()> {
        let Some((_,old))=self.rows.0.first()else{return Err(Error::STATE)};
        let mut node=old.clone();node.label=caption(&format!("{}: {value}",self.label),127);node.enabled=!pending;
        node.help=caption(notice,255);self.rows.apply(cx,vec![node])
    }
    pub fn value(&self)->&str{&self.value}
    pub fn set(&mut self,cx:&mut Cx,value:&str)->Result<()> {
        if self.pending.is_some(){return Err(Error::STATE)}
        if value.len()>self.max_bytes||value.chars().any(|c|c<' '||c=='\x7f'){return Err(Error::ARG)}
        self.display(cx,value,false,"Value updated. Select to edit; Enter/Cross accepts, Escape/Circle cancels.")?;
        self.value=value.into();Ok(())
    }
    pub fn handle(&mut self,cx:&mut Cx,event:&MenuEvent)->Result<bool>{
        if self.rows.activated(event).is_none()||self.pending.is_some(){return Ok(false)}
        match cx.text_entry(&self.label,&self.value,self.max_bytes){
            Ok(mut dialog)=>{
                if let Err(error)=self.display(cx,&self.value.clone(),true,"Editing in system dialog. Enter/Cross accepts; Escape/Circle cancels."){
                    let _=dialog.cancel(cx);return Err(error)
                }
                self.pending=Some(dialog);Ok(true)
            },
            Err(error)=>{self.display(cx,&self.value.clone(),false,&format!("Cannot open text dialog: {error}. Another game or mod dialog may own input; retry when it closes."))?;Err(error)}
        }
    }
    pub fn poll(&mut self,cx:&mut Cx)->Result<Option<String>>{
        let Some(dialog)=self.pending.as_mut()else{return Ok(None)};
        match dialog.poll(cx)? {
            crate::text_entry::State::Pending=>Ok(None),
            crate::text_entry::State::Accepted(value)=>{
                self.display(cx,&value,false,"Value accepted. Select to edit; Escape/Circle cancels without changing it.")?;
                self.value=value.clone();self.pending=None;Ok(Some(value))
            },
            crate::text_entry::State::Cancelled=>{
                self.display(cx,&self.value.clone(),false,"Edit cancelled; value unchanged. Select to edit again.")?;
                self.pending=None;Ok(None)
            },
            crate::text_entry::State::Failed(error)=>{
                self.display(cx,&self.value.clone(),false,&format!("Text entry failed: {error}. Value unchanged; select to retry."))?;
                self.pending=None;Err(error)
            },
        }
    }
    pub fn release(&mut self,cx:&Cx)->Result<()> {
        if let Some(dialog)=&mut self.pending {dialog.cancel(cx)?;}
        self.pending=None;self.rows.release(cx)
    }
}
impl Integer {
    pub fn register(cx:&mut Cx,path:&str,parent:&str,label:&str,min:i32,max:i32,step:i32,value:i32)->Result<Self>{
        if min>max||step<=0||value<min||value>max{return Err(Error::ARG)}
        let mut control=Self{rows:Rows(Vec::new()),path:path.into(),parent:parent.into(),label:label.into(),min,max,step,value};
        control.rows=Rows::register(cx,control.nodes(value)?)?;Ok(control)
    }
    fn adjusted(&self,value:i32,multiplier:i64)->i32{
        (value as i64+self.step as i64*multiplier).clamp(self.min as i64,self.max as i64) as i32
    }
    fn nodes(&self,value:i32)->Result<Vec<Node>>{
        let mut root=screen(&self.path,&self.parent,&format!("{}: {value}",self.label))?;
        root.help=format!("Range {} to {}. Open to adjust; changes apply immediately.",self.min,self.max);
        let prefix=root.key.clone();let mut nodes=vec![root];
        let mut current=Node::new(Kind::Info,&format!("{prefix}-value"),&self.path,&format!("{}: {value}",self.label));
        current.order=-1;current.help=format!("Accepted value. Range {} to {}. Back returns to the previous menu.",self.min,self.max);nodes.push(current);
        for (i,multiplier) in [-10i64,-1,1,10].into_iter().enumerate(){
            let delta=self.step as i64*multiplier;let next=self.adjusted(value,multiplier);
            let mut node=Node::new(Kind::Action,&format!("{prefix}-adjust-{i}"),&self.path,&format!("{} by {}",if delta<0{"Decrease"}else{"Increase"},delta.abs()));
            node.order=i as i32;node.enabled=next!=value;node.help=if node.enabled{format!("Apply {next}. Range {} to {}.",self.min,self.max)}else{format!("Already at the {} value ({value}).",if delta<0{"minimum"}else{"maximum"})};nodes.push(node);
        }
        Ok(nodes)
    }
    pub fn value(&self)->i32{self.value}
    pub fn set(&mut self,cx:&mut Cx,value:i32)->Result<()> {
        if value<self.min||value>self.max{return Err(Error::ARG)}
        self.rows.apply(cx,self.nodes(value)?)?;self.value=value;Ok(())
    }
    pub fn handle(&mut self,cx:&mut Cx,event:&MenuEvent)->Result<Option<i32>>{
        let Some(index)=self.rows.activated(event) else{return Ok(None)};
        if index<2{return Ok(None)}
        let value=self.adjusted(self.value,[-10,-1,1,10][index-2]);
        self.set(cx,value)?;Ok(Some(value))
    }
    pub fn release(&mut self,cx:&Cx)->Result<()>{self.rows.release(cx)}
}

#[cfg(test)] mod tests {
    use super::*;
    #[test] fn full_integer_range_never_wraps(){
        let c=Integer{rows:Rows(Vec::new()),path:String::new(),parent:String::new(),label:String::new(),min:i32::MIN,max:i32::MAX,step:i32::MAX,value:0};
        assert_eq!(c.adjusted(i32::MAX,10),i32::MAX);
        assert_eq!(c.adjusted(i32::MIN,-10),i32::MIN);
        assert_eq!(c.adjusted(i32::MIN,1),-1);
        assert_eq!(c.adjusted(i32::MAX,-1),0);
    }
    #[test] fn invalid_values_are_rejected_before_using_host(){
        let mut cx=Cx::new(core::ptr::null(),core::ptr::null_mut());
        assert!(matches!(Choice::register(&mut cx,"a/b","game.mods","Choice",&[],0),Err(Error::ARG)));
        assert!(matches!(Choice::register(&mut cx,"a/b","game.mods","Choice",&["A"],1),Err(Error::ARG)));
        for (min,max,step,value) in [(1,0,1,0),(0,100,0,50),(0,100,-1,50),(0,100,1,101)] {
            assert!(matches!(Integer::register(&mut cx,"a/b","game.mods","Level",min,max,step,value),Err(Error::ARG)));
        }
    }
}
