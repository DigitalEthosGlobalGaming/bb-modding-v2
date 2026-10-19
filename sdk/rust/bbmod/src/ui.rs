use crate::sys;
use std::ffi::CString;

/// Widgets, valid only inside a panel's draw closure.
pub struct Ui {
    host: *const sys::bbm_host,
    me: sys::bbm_handle,
    cx: *const crate::Cx,
}

#[cfg(test)]mod table_tests{
    use super::*;
    use std::sync::atomic::{AtomicUsize,Ordering};
    static ENDS:AtomicUsize=AtomicUsize::new(0);
    unsafe extern "C" fn begin(_:sys::bbm_handle,_:*const core::ffi::c_char,_:*const *const core::ffi::c_char,_:u32,_:f32,col:*mut i32,desc:*mut i32)->i32{
        unsafe{col.write(1);desc.write(1);}1
    }
    unsafe extern "C" fn noop(_:sys::bbm_handle){}
    unsafe extern "C" fn end(_:sys::bbm_handle){ENDS.fetch_add(1,Ordering::SeqCst);}
    #[test]fn table_unwind_balances_and_old_host_rejected(){
        let mut storage=core::mem::MaybeUninit::<sys::bbm_host>::uninit();let h=storage.as_mut_ptr();
        unsafe{
            core::ptr::addr_of_mut!((*h).size).write(core::mem::size_of::<sys::bbm_host>() as u32);
            core::ptr::addr_of_mut!((*h).ui_table_begin).write(begin);
            core::ptr::addr_of_mut!((*h).ui_table_next_row).write(noop);
            core::ptr::addr_of_mut!((*h).ui_table_next_column).write(noop);
            core::ptr::addr_of_mut!((*h).ui_table_end).write(end);
        }
        let ui=Ui::new(h,core::ptr::null_mut(),core::ptr::null());let(mut sort,mut desc)=(0,false);
        let result=std::panic::catch_unwind(std::panic::AssertUnwindSafe(||{
            let table=ui.table("test",&["Name","HP"],300.0,&mut sort,&mut desc).unwrap();table.next_row();table.next_column();panic!("test draw panic");
        }));
        assert!(result.is_err()&&sort==1&&desc);assert_eq!(ENDS.load(Ordering::SeqCst),1);
        let old=[8usize];let ui=Ui::new(old.as_ptr().cast(),core::ptr::null_mut(),core::ptr::null());
        assert!(ui.table("old",&["HP"],300.0,&mut sort,&mut desc).is_none());
    }
}

/// A visible table. Dropping it balances the host's table scope, including unwind.
pub struct Table<'a> { ui: &'a Ui }
impl Table<'_> {
    pub fn next_row(&self) { unsafe { (core::ptr::addr_of!((*self.ui.host).ui_table_next_row).read())(self.ui.me) } }
    pub fn next_column(&self) { unsafe { (core::ptr::addr_of!((*self.ui.host).ui_table_next_column).read())(self.ui.me) } }
}
impl Drop for Table<'_> {
    fn drop(&mut self) { unsafe { (core::ptr::addr_of!((*self.ui.host).ui_table_end).read())(self.ui.me) } }
}

impl Ui {
    /// Begin a scrollable/resizable table with clickable single-column sorting.
    /// Sort outputs are valid before drawing rows. None means clipped or an older
    /// backend. Only one table may be open per mod draw; keep other widgets outside.
    pub fn table(&self,id:&str,headers:&[&str],height:f32,sort:&mut usize,descending:&mut bool)->Option<Table<'_>> {
        let size=unsafe{core::ptr::addr_of!((*self.host).size).read()} as usize;
        if size<core::mem::offset_of!(sys::bbm_host,ui_table_end)+core::mem::size_of::<usize>()||headers.is_empty()||headers.len()>32{return None;}
        let id=Self::c(id);let labels:Vec<_>=headers.iter().map(|s|Self::c(s)).collect();
        let pointers:Vec<_>=labels.iter().map(|s|s.as_ptr()).collect();
        let mut col=(*sort).min(headers.len()-1) as i32;let mut dir=*descending as i32;
        let visible=unsafe{(core::ptr::addr_of!((*self.host).ui_table_begin).read())(self.me,id.as_ptr(),pointers.as_ptr(),headers.len() as u32,height,&mut col,&mut dir)};
        if visible!=1{return None;}
        *sort=(col.max(0) as usize).min(headers.len()-1);*descending=dir!=0;Some(Table{ui:self})
    }
    pub(crate) fn new(host: *const sys::bbm_host, me: sys::bbm_handle, cx: *const crate::Cx) -> Ui {
        Ui { host, me, cx }
    }
    /// The mod's `Cx`, to patch, read memory or log from a panel.
    pub fn cx(&self) -> &crate::Cx { unsafe { &*self.cx } }
    fn h(&self) -> &sys::bbm_host { unsafe { &*self.host } }
    fn c(s: &str) -> CString { CString::new(s.replace('\0', "")).unwrap_or_default() }

    pub fn text(&self, s: &str) { let c = Self::c(s); unsafe { (self.h().ui_text)(self.me, c.as_ptr()) } }
    pub fn button(&self, label: &str) -> bool {
        let c = Self::c(label);
        unsafe { (self.h().ui_button)(self.me, c.as_ptr()) != 0 }
    }
    pub fn checkbox(&self, label: &str, value: &mut bool) -> bool {
        let c = Self::c(label);
        let mut v = *value as i32;
        let changed = unsafe { (self.h().ui_checkbox)(self.me, c.as_ptr(), &mut v) != 0 };
        *value = v != 0;
        changed
    }
    pub fn slider_f32(&self, label: &str, value: &mut f32, min: f32, max: f32) -> bool {
        let c = Self::c(label);
        unsafe { (self.h().ui_slider_float)(self.me, c.as_ptr(), value, min, max) != 0 }
    }
    pub fn separator(&self) { unsafe { (self.h().ui_separator)(self.me) } }
    pub fn same_line(&self) { unsafe { (self.h().ui_same_line)(self.me) } }

    // ---- ABI 1.1 widgets (fall back gracefully on a 1.0 host) --------------

    pub fn slider_i32(&self, label: &str, value: &mut i32, min: i32, max: i32) -> bool {
        if !self.h().has_v1_1() { return false; }
        let c = Self::c(label);
        unsafe { (self.h().ui_slider_int)(self.me, c.as_ptr(), value, min, max) != 0 }
    }

    /// Single-line text box editing `value` (at most `cap - 1` bytes). True when edited.
    pub fn input_text(&self, label: &str, value: &mut String, cap: usize) -> bool {
        if !self.h().has_v1_1() { return false; }
        let c = Self::c(label);
        let mut buf = vec![0u8; cap.max(2)];
        let n = value.len().min(buf.len() - 1);
        buf[..n].copy_from_slice(&value.as_bytes()[..n]);
        let changed = unsafe { (self.h().ui_input_text)(self.me, c.as_ptr(), buf.as_mut_ptr().cast(), buf.len()) != 0 };
        if changed {
            let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
            *value = String::from_utf8_lossy(&buf[..end]).into_owned();
        }
        changed
    }

    /// A foldable section; draw its contents only when this returns true.
    pub fn collapsing_header(&self, label: &str) -> bool {
        if !self.h().has_v1_1() { return true; }
        let c = Self::c(label);
        unsafe { (self.h().ui_collapsing_header)(self.me, c.as_ptr()) != 0 }
    }

    /// Drop-down; `current` indexes `items`. True when the choice changed.
    pub fn combo(&self, label: &str, current: &mut usize, items: &[&str]) -> bool {
        if !self.h().has_v1_1() || items.is_empty() { return false; }
        let c = Self::c(label);
        let mut list = Vec::new();
        for it in items {
            list.extend(it.bytes().filter(|&b| b != 0));
            list.push(0);
        }
        list.push(0);
        let mut cur = (*current).min(items.len() - 1) as i32;
        let changed = unsafe { (self.h().ui_combo)(self.me, c.as_ptr(), &mut cur, list.as_ptr().cast()) != 0 };
        *current = cur.max(0) as usize;
        changed
    }

    /// Coloured text, RGBA in 0..1 (`[1.0, 0.4, 0.4, 1.0]` for a warning).
    pub fn text_colored(&self, rgba: [f32; 4], s: &str) {
        let c = Self::c(s);
        if self.h().has_v1_1() {
            unsafe { (self.h().ui_text_colored)(self.me, rgba[0], rgba[1], rgba[2], rgba[3], c.as_ptr()) }
        } else {
            unsafe { (self.h().ui_text)(self.me, c.as_ptr()) }
        }
    }
}
