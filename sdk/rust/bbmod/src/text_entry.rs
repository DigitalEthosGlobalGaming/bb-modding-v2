//! Host-owned system text dialog, ABI 1.10. The provider shares the game's
//! system IME; an existing game/mod dialog returns CONFLICT. Poll at the host
//! sync point. No mod pointer or callback enters the window's input thread.
use crate::{check,sys,Cx,Error,Id,Result};

#[derive(Debug,Clone,PartialEq,Eq)]
pub enum State {Pending,Accepted(String),Cancelled,Failed(Error)}
pub struct TextEntry {id:Option<Id>,state:State}
fn copy(text:&str,out:&mut[core::ffi::c_char])->Result<()> {
    if text.len()>=out.len()||text.as_bytes().contains(&0){return Err(Error::ARG)}
    for (a,b) in out.iter_mut().zip(text.bytes()){*a=b as core::ffi::c_char;}Ok(())
}
impl Cx {
    pub fn text_entry(&mut self,title:&str,initial:&str,max_bytes:usize)->Result<TextEntry>{
        let h=unsafe{&*self.host};if !h.has_text_entry(){return Err(Error::UNSUPPORTED)}
        if !(1..=4095).contains(&max_bytes)||initial.len()>max_bytes{return Err(Error::ARG)}
        let mut request=sys::bbm_text_request{size:core::mem::size_of::<sys::bbm_text_request>() as u32,max_bytes:max_bytes as u32,..Default::default()};
        copy(title,&mut request.title)?;copy(initial,&mut request.initial)?;
        let mut id=0;check(unsafe{(h.text_begin)(self.me,&request,&mut id)})?;
        if id==0{return Err(Error::STATE)}
        Ok(TextEntry{id:Some(Id(id)),state:State::Pending})
    }
}
impl TextEntry {
    /// Stable terminal results. Releases the owned host resource on completion.
    pub fn poll(&mut self,cx:&Cx)->Result<State>{
        let Some(id)=self.id else{return Ok(self.state.clone())};
        let h=unsafe{&*cx.host};if !h.has_text_entry(){return Err(Error::UNSUPPORTED)}
        let mut result=sys::bbm_text_result::default();check(unsafe{(h.text_poll)(cx.me,id.0,&mut result)})?;
        if result.size as usize!=core::mem::size_of_val(&result)||result.reserved!=0{return Err(Error::STATE)}
        let state=match result.state {
            0=>State::Pending,
            1=>{let end=result.text.iter().position(|x|*x==0).ok_or(Error::STATE)?;
                State::Accepted(String::from_utf8(result.text[..end].iter().map(|c|*c as u8).collect()).map_err(|_|Error::STATE)?)},
            2=>State::Cancelled,3=>State::Failed(Error(result.status)),_=>return Err(Error::STATE),
        };
        if state!=State::Pending {cx.release(id)?;self.id=None;}
        self.state=state.clone();Ok(state)
    }
    /// Cancel before replacing or discarding a pending dialog. If this handle
    /// is dropped without cancellation, unload still releases its host resource.
    pub fn cancel(&mut self,cx:&Cx)->Result<()> {
        if let Some(id)=self.id {cx.release(id)?;self.id=None;self.state=State::Cancelled;}
        Ok(())
    }
}

#[cfg(test)] mod tests {
    use super::*;
    #[test]fn c_layout_and_utf8_limits(){
        assert_eq!(core::mem::size_of::<sys::bbm_text_request>(),4232);
        assert_eq!(core::mem::size_of::<sys::bbm_text_result>(),4112);
        let mut out=[0;8];assert!(copy("✓✓",&mut out).is_ok());
        assert_eq!(copy("✓✓✓",&mut out),Err(Error::ARG));
        assert_eq!(copy("a\0b",&mut out),Err(Error::ARG));
    }
}
