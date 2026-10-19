use crate::{cx::Cx, cx::Event, detour::INFLIGHT, sys, Error, Result, Version};
use core::sync::atomic::{AtomicI32, Ordering};
use std::panic::{catch_unwind, AssertUnwindSafe};

/// Implement this for your mod's state struct, then call `bbmod::export!(YourMod)`.
pub trait Mod: Sized + 'static {
    /// Called on first load and after every hot reload. Register patches, hooks,
    /// panels and callbacks here through `cx`; the host releases them on unload.
    fn load(cx: &mut Cx) -> Result<Self>;

    /// Called before the host reverts your resources. Optional.
    fn unload(&mut self, _cx: &mut Cx) {}

    /// Bytes to carry across a hot reload (serialize however you like).
    fn save_state(&self) -> Option<Vec<u8>> { None }

    /// Called right after `load` on a reload, with the bytes `save_state` produced.
    /// The old instance's patches and hooks are already reverted: re-apply what
    /// the state says should be on through `cx`.
    fn restore_state(&mut self, _cx: &mut Cx, _state: &[u8], _from: Version) {}

    /// Events selected with `cx.subscribe`.
    fn event(&mut self, _cx: &mut Cx, _event: &Event<'_>) {}
}

/// Runs `f`, turning a panic into a log line instead of unwinding into the game.
pub(crate) fn guard<R>(what: &str, f: impl FnOnce() -> R) -> Option<R> {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(r) => Some(r),
        Err(_) => {
            eprintln!("[bbmod] panic in {what}; call skipped");
            None
        }
    }
}

struct Instance<M: Mod> {
    cx: Cx,
    m: Option<M>,
}

#[doc(hidden)]
pub mod __internal {
    use super::*;

    unsafe fn inst<'a, M: Mod>(ctx: *mut sys::bbm_ctx) -> &'a mut Instance<M> {
        &mut *(ctx as *mut Instance<M>)
    }

    pub unsafe extern "C" fn on_load<M: Mod>(ctx: *mut sys::bbm_ctx) -> i32 {
        let i = inst::<M>(ctx);
        INFLIGHT.store(i.cx.inflight_ptr().cast::<AtomicI32>(), Ordering::Release);
        match guard("load", || M::load(&mut i.cx)) {
            Some(Ok(m)) => { i.m = Some(m); sys::BBM_OK }
            Some(Err(Error(rc))) => { i.cx.log_error(&format!("load failed: {}", Error(rc))); rc }
            None => { i.cx.log_error("load panicked"); sys::BBM_ERR }
        }
    }

    pub unsafe extern "C" fn on_unload<M: Mod>(ctx: *mut sys::bbm_ctx) {
        let i = inst::<M>(ctx);
        if let Some(mut m) = i.m.take() {
            guard("unload", || m.unload(&mut i.cx));
        }
    }

    pub unsafe extern "C" fn on_save<M: Mod>(ctx: *mut sys::bbm_ctx, out: *mut sys::bbm_blob) -> i32 {
        let i = inst::<M>(ctx);
        let Some(m) = i.m.as_ref() else { return sys::BBM_ERR_STATE };
        let Some(Some(bytes)) = guard("save_state", || m.save_state()) else { return sys::BBM_ERR_NOTFOUND };
        let host = &*i.cx.host;
        let p = (host.alloc)(bytes.len().max(1)).cast::<u8>();
        if p.is_null() { return sys::BBM_ERR_NOMEM; }
        core::ptr::copy_nonoverlapping(bytes.as_ptr(), p, bytes.len());
        (*out).data = p;
        (*out).len = bytes.len();
        sys::BBM_OK
    }

    pub unsafe extern "C" fn on_restore<M: Mod>(
        ctx: *mut sys::bbm_ctx,
        state: *const sys::bbm_blob,
        from: Version,
    ) -> i32 {
        let i = inst::<M>(ctx);
        let Some(m) = i.m.as_mut() else { return sys::BBM_ERR_STATE };
        let bytes = core::slice::from_raw_parts((*state).data, (*state).len);
        let cx = &mut i.cx;
        guard("restore_state", || m.restore_state(cx, bytes, from));
        sys::BBM_OK
    }

    pub unsafe extern "C" fn on_event<M: Mod>(ctx: *mut sys::bbm_ctx, ev: *const sys::bbm_event) {
        let i = inst::<M>(ctx);
        let e = &*ev;
        let data = if e.data.is_null() { &[][..] } else { core::slice::from_raw_parts(e.data.cast::<u8>(), e.data_len) };
        let event = Event { kind: e.type_, a: e.a, b: e.b, data };
        if let Some(m) = i.m.as_mut() {
            guard("event", || m.event(&mut i.cx, &event));
        }
    }

    pub unsafe extern "C" fn destroy<M: Mod>(ctx: *mut sys::bbm_ctx) {
        INFLIGHT.store(core::ptr::null_mut(), Ordering::Release);
        // Host has released every callback by now, so closures can go.
        drop(Box::from_raw(ctx as *mut Instance<M>));
    }

    pub unsafe fn entry<M: Mod>(
        host: *const sys::bbm_host,
        me: sys::bbm_handle,
        api: *mut sys::bbm_mod_api,
        out_ctx: *mut *mut sys::bbm_ctx,
    ) -> i32 {
        if host.is_null() || (*host).abi_major != sys::BBM_ABI_MAJOR {
            return sys::BBM_ERR;
        }
        let b = Box::new(Instance::<M> { cx: Cx::new(host, me), m: None });
        *out_ctx = Box::into_raw(b).cast();
        let a = &mut *api;
        a.size = core::mem::size_of::<sys::bbm_mod_api>() as u32;
        a.abi_major = sys::BBM_ABI_MAJOR;
        a.abi_minor = sys::BBM_ABI_MINOR;
        a.on_load = Some(on_load::<M>);
        a.on_unload = Some(on_unload::<M>);
        a.on_save_state = Some(on_save::<M>);
        a.on_restore_state = Some(on_restore::<M>);
        a.on_event = Some(on_event::<M>);
        a.destroy = Some(destroy::<M>);
        sys::BBM_OK
    }
}

/// Generates the `bbmod_entry` symbol for your `Mod` type.
#[macro_export]
macro_rules! export {
    ($ty:ty) => {
        #[no_mangle]
        pub unsafe extern "C" fn bbmod_entry(
            host: *const $crate::sys::bbm_host,
            me: $crate::sys::bbm_handle,
            api: *mut $crate::sys::bbm_mod_api,
            out_ctx: *mut *mut $crate::sys::bbm_ctx,
        ) -> i32 {
            $crate::__internal::entry::<$ty>(host, me, api, out_ctx)
        }
    };
}
