use crate::{check, sys, ui::Ui, Result};
use core::ffi::{c_char, c_void, CStr};
use std::any::Any;
use std::ffi::CString;

/// Fault-safe memory reads from any thread. Obtained from `Cx::reader`; valid
/// while the mod is loaded (host threads from `cx.spawn` are joined first).
#[derive(Clone, Copy)]
pub struct Reader {
    host: *const sys::bbm_host,
    me: sys::bbm_handle,
}
// SAFETY: the host's read and mem_region calls are thread-safe and stateless.
unsafe impl Send for Reader {}
unsafe impl Sync for Reader {}

impl Reader {
    fn h(&self) -> &sys::bbm_host { unsafe { &*self.host } }

    pub fn read(&self, addr: usize, out: &mut [u8]) -> Result<()> {
        check(unsafe { (self.h().read)(self.me, addr, out.as_mut_ptr().cast(), out.len()) })
    }

    pub fn read_val<T: Copy>(&self, addr: usize) -> Result<T> {
        let mut v = core::mem::MaybeUninit::<T>::uninit();
        let b = unsafe { core::slice::from_raw_parts_mut(v.as_mut_ptr().cast::<u8>(), core::mem::size_of::<T>()) };
        self.read(addr, b)?;
        Ok(unsafe { v.assume_init() })
    }

    /// Readable regions, lowest first.
    pub fn regions(&self) -> Vec<sys::bbm_region> {
        let mut out = Vec::new();
        if !self.h().has_v1_2() { return out; }
        let mut addr = 0usize;
        loop {
            let mut r = sys::bbm_region::default();
            if unsafe { (self.h().mem_region)(self.me, addr, &mut r) } != sys::BBM_OK { break; }
            out.push(r);
            match r.base.checked_add(r.size) {
                Some(next) if next > addr => addr = next,
                _ => break,
            }
        }
        out
    }
}

/// Handle to a host-owned resource (patch, hook, callback, panel, ...).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Id(pub u64);

/// A ModNet channel.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Channel(pub u64);

pub type SessionInfo = sys::bbm_session_info;

/// Game or session event delivered to `Mod::event`.
#[derive(Debug, Clone, Copy)]
pub struct Event<'a> {
    pub kind: u32,
    pub a: u64,
    pub b: u64,
    pub data: &'a [u8],
}

/// Everything a mod can ask of the host. One per loaded instance.
pub struct Cx {
    pub(crate) host: *const sys::bbm_host,
    pub(crate) me: sys::bbm_handle,
    /// Closures given to the host. Dropped in `destroy`, after the host has
    /// released every resource that could still call them.
    pub(crate) owned: Vec<Box<dyn Any>>,
}

fn cstr(s: &str) -> Result<CString> {
    CString::new(s).map_err(|_| crate::Error::ARG)
}

impl Cx {
    pub(crate) fn new(host: *const sys::bbm_host, me: sys::bbm_handle) -> Cx {
        Cx { host, me, owned: Vec::new() }
    }

    #[inline]
    fn h(&self) -> &sys::bbm_host {
        unsafe { &*self.host }
    }

    // ---- Core -------------------------------------------------------------

    pub fn log(&self, level: i32, msg: &str) {
        if let Ok(c) = cstr(msg) {
            unsafe { (self.h().log)(self.me, level, c.as_ptr()) }
        }
    }
    pub fn log_debug(&self, msg: &str) { self.log(sys::BBM_LOG_DEBUG, msg) }
    pub fn log_info(&self, msg: &str) { self.log(sys::BBM_LOG_INFO, msg) }
    pub fn log_warn(&self, msg: &str) { self.log(sys::BBM_LOG_WARN, msg) }
    pub fn log_error(&self, msg: &str) { self.log(sys::BBM_LOG_ERROR, msg) }

    pub fn last_error(&self) -> String {
        unsafe {
            let p = (self.h().last_error)(self.me);
            if p.is_null() { String::new() } else { CStr::from_ptr(p).to_string_lossy().into_owned() }
        }
    }

    /// Reads `section.key` from this mod's `mod.toml` (or a user override).
    pub fn config(&self, key: &str) -> Option<String> {
        let k = cstr(key).ok()?;
        let mut buf = [0 as c_char; 512];
        let rc = unsafe { (self.h().config_get)(self.me, k.as_ptr(), buf.as_mut_ptr(), buf.len()) };
        if rc != sys::BBM_OK { return None; }
        Some(unsafe { CStr::from_ptr(buf.as_ptr()) }.to_string_lossy().into_owned())
    }

    pub fn version(&self) -> crate::Version {
        let mut id: *const c_char = core::ptr::null();
        let mut v = crate::Version::default();
        unsafe { (self.h().mod_info)(self.me, &mut id, &mut v) };
        v
    }

    pub fn release(&self, id: Id) -> Result<()> {
        check(unsafe { (self.h().release)(self.me, id.0) })
    }

    pub(crate) fn inflight_ptr(&self) -> *mut i32 {
        unsafe { (self.h().inflight)(self.me) }
    }

    /// `section.key` from `mod.toml`, parsed (`cx.config_parse::<f32>("tuning.speed")`).
    pub fn config_parse<T: core::str::FromStr>(&self, key: &str) -> Option<T> {
        self.config(key)?.trim().parse().ok()
    }

    /// `true`/`1`/`yes`/`on` (any case) from `mod.toml`; `default` if missing.
    pub fn config_bool(&self, key: &str, default: bool) -> bool {
        match self.config(key) {
            Some(v) => matches!(v.trim().to_ascii_lowercase().as_str(), "true" | "1" | "yes" | "on"),
            None => default,
        }
    }

    // ---- Memory -----------------------------------------------------------

    pub fn eboot_base(&self) -> usize { unsafe { (self.h().eboot_base)(self.me) } }

    /// PS4 virtual address (eboot at 0x400000) to a host address.
    pub fn ps4_to_host(&self, ps4: u64) -> usize { unsafe { (self.h().ps4_to_host)(self.me, ps4) } }

    /// Bytes of the eboot image (code, data, bss) from `eboot_base()`; 0 on a pre-1.2 host.
    pub fn eboot_size(&self) -> usize {
        if self.h().has_v1_2() { unsafe { (self.h().eboot_size)(self.me) } } else { 0 }
    }

    /// True if `addr` is inside the eboot image (so a PS4 address makes sense for it).
    pub fn in_eboot(&self, addr: usize) -> bool {
        let base = self.eboot_base();
        addr >= base && addr - base < self.eboot_size()
    }

    /// `"0x04d99138"` for an address in the eboot image (its PS4 address, as in
    /// Ghidra/IDA/XML), `"h0x7ff6..."` for anything else (heap, stacks, DLLs).
    pub fn fmt_addr(&self, addr: usize) -> String {
        if self.in_eboot(addr) {
            format!("0x{:x}", (addr - self.eboot_base()) as u64 + 0x400000)
        } else {
            format!("h0x{addr:x}")
        }
    }

    /// Parses what `fmt_addr` prints: `"0x..."` / `"..."` = PS4 address, `"h0x..."` = host.
    pub fn parse_addr(&self, s: &str) -> Option<usize> {
        let s = s.trim();
        let (host, s) = match s.strip_prefix('h').or_else(|| s.strip_prefix('H')) {
            Some(rest) => (true, rest),
            None => (false, s),
        };
        let digits = s.trim_start_matches("0x").trim_start_matches("0X");
        if digits.is_empty() { return None; }
        let v = u64::from_str_radix(digits, 16).ok()?;
        Some(if host { v as usize } else { self.ps4(v) })
    }

    /// Readable memory regions, lowest first (memory scanners). Empty on a pre-1.2 host.
    pub fn regions(&self) -> Vec<crate::sys::bbm_region> { self.reader().regions() }

    /// A `Send + Sync` handle for fault-safe reads from any thread (e.g. a scan in
    /// `cx.spawn`). Valid while the mod is loaded.
    pub fn reader(&self) -> Reader { Reader { host: self.host, me: self.me } }

    /// Short for `ps4_to_host`: addresses copied from XML patches, Ghidra or IDA
    /// (with the eboot loaded at 0x400000) work as they are: `cx.ps4(0x04d99138)`.
    pub fn ps4(&self, ps4: u64) -> usize { self.ps4_to_host(ps4) }

    /// Fault-safe typed read, e.g. `cx.read_val::<f32>(addr)?`.
    pub fn read_val<T: Copy>(&self, addr: usize) -> Result<T> {
        let mut v = core::mem::MaybeUninit::<T>::uninit();
        let bytes = unsafe { core::slice::from_raw_parts_mut(v.as_mut_ptr().cast::<u8>(), core::mem::size_of::<T>()) };
        self.read(addr, bytes)?;
        Ok(unsafe { v.assume_init() })
    }

    /// Unowned typed write (game data such as health or position; not reverted on unload).
    pub fn write_val<T: Copy>(&self, addr: usize, v: T) -> Result<()> {
        let bytes = unsafe { core::slice::from_raw_parts((&v as *const T).cast::<u8>(), core::mem::size_of::<T>()) };
        self.write(addr, bytes)
    }

    /// Owned typed patch, reverted on unload.
    pub fn patch_val<T: Copy>(&self, addr: usize, v: T) -> Result<Id> {
        let bytes = unsafe { core::slice::from_raw_parts((&v as *const T).cast::<u8>(), core::mem::size_of::<T>()) };
        self.patch(addr, bytes)
    }

    pub fn read_ptr(&self, addr: usize) -> Result<usize> { Ok(self.read_val::<u64>(addr)? as usize) }

    /// Cheat Engine style pointer chain: `[[[base] + o0] + o1] + o2`.
    /// Reads a pointer at `base`, then for every offset but the last adds it and
    /// reads again; the last offset is added without a read. A null link is an error.
    pub fn follow(&self, base: usize, offsets: &[usize]) -> Result<usize> {
        let mut p = self.read_ptr(base)?;
        for (i, off) in offsets.iter().enumerate() {
            if p == 0 { return Err(crate::Error::NOT_FOUND); }
            if i + 1 == offsets.len() { return Ok(p + off); }
            p = self.read_ptr(p + off)?;
        }
        Ok(p)
    }

    /// Fault-safe read.
    pub fn read(&self, addr: usize, out: &mut [u8]) -> Result<()> {
        check(unsafe { (self.h().read)(self.me, addr, out.as_mut_ptr().cast(), out.len()) })
    }

    pub fn read_u32(&self, addr: usize) -> Result<u32> {
        let mut b = [0u8; 4];
        self.read(addr, &mut b)?;
        Ok(u32::from_le_bytes(b))
    }

    pub fn read_f32(&self, addr: usize) -> Result<f32> { Ok(f32::from_bits(self.read_u32(addr)?)) }

    /// Unowned data write (not reverted on unload). Use `patch` for code.
    pub fn write(&self, addr: usize, data: &[u8]) -> Result<()> {
        check(unsafe { (self.h().write)(self.me, addr, data.as_ptr().cast(), data.len()) })
    }

    /// IDA-style pattern scan, e.g. `"48 8B ?? ?? 89"`. `len == 0` scans the whole eboot.
    pub fn scan(&self, pattern: &str, start: usize, len: usize) -> Result<usize> {
        let p = cstr(pattern)?;
        let mut out = 0usize;
        check(unsafe { (self.h().scan)(self.me, p.as_ptr(), start, len, &mut out) })?;
        Ok(out)
    }

    // ---- Patches ----------------------------------------------------------

    /// Owned byte patch; reverted automatically on unload.
    pub fn patch(&self, addr: usize, bytes: &[u8]) -> Result<Id> {
        let mut id = 0u64;
        check(unsafe { (self.h().patch_bytes)(self.me, addr, bytes.as_ptr().cast(), bytes.len(), &mut id) })?;
        Ok(Id(id))
    }

    /// Applies a shadPS4/GoldHEN XML patch file (path relative to the mod folder).
    pub fn patch_xml(&self, path: &str, name: Option<&str>) -> Result<Id> {
        let p = cstr(path)?;
        let n = name.map(cstr).transpose()?;
        let mut id = 0u64;
        check(unsafe {
            (self.h().patch_xml)(self.me, p.as_ptr(), n.as_ref().map_or(core::ptr::null(), |c| c.as_ptr()), &mut id)
        })?;
        Ok(Id(id))
    }

    // ---- Hooks ------------------------------------------------------------

    /// Inline hook. `detour` must be an `extern "sysv64"` function.
    ///
    /// # Safety
    /// `target` must be the start of a function and `detour` must match its signature.
    pub unsafe fn hook_inline(&self, target: usize, detour: *mut c_void, d: &crate::Detour) -> Result<Id> {
        let mut tramp: *mut c_void = core::ptr::null_mut();
        let mut id = 0u64;
        check((self.h().hook_inline)(self.me, target, detour, &mut tramp, &mut id))?;
        d.set(tramp);
        Ok(Id(id))
    }

    /// Like `hook_inline`, but the trampoline (original) pointer is written to
    /// `out_tramp` instead of a `Detour`, and the host writes it *before* the
    /// target is patched. For hooks whose detour reads the trampoline from a
    /// fixed slot (the inspector's asm thunks) so there is no null window.
    ///
    /// # Safety
    /// As `hook_inline`. `out_tramp` must point at a writable `*mut c_void`.
    pub unsafe fn hook_inline_raw(&self, target: usize, detour: *mut c_void,
                                  out_tramp: *mut *mut c_void) -> Result<Id> {
        let mut id = 0u64;
        check((self.h().hook_inline)(self.me, target, detour, out_tramp, &mut id))?;
        Ok(Id(id))
    }

    /// Pointer-slot hook (vtable entry, function table).
    ///
    /// # Safety
    /// `slot` must point at a valid function pointer slot.
    pub unsafe fn hook_slot(&self, slot: *mut *mut c_void, detour: *mut c_void, d: &crate::Detour) -> Result<Id> {
        let mut orig: *mut c_void = core::ptr::null_mut();
        let mut id = 0u64;
        check((self.h().hook_slot)(self.me, slot, detour, &mut orig, &mut id))?;
        d.set(orig);
        Ok(Id(id))
    }

    // ---- Scheduling -------------------------------------------------------

    /// Runs `f(cx)` on the game thread every frame, at the sync point.
    pub fn on_frame(&mut self, f: impl FnMut(&Cx) + 'static) -> Result<Id> {
        struct F { cx: *const Cx, f: Box<dyn FnMut(&Cx)> }
        unsafe extern "C" fn tramp(user: *mut c_void) {
            let s = &mut *(user as *mut F);
            let cx = &*s.cx;
            let f = &mut s.f;
            crate::entry::guard("frame callback", || f(cx));
        }
        // `self` lives in the boxed instance and never moves; callbacks end before it drops.
        let mut b = Box::new(F { cx: self as *const Cx, f: Box::new(f) });
        let user = (&mut *b as *mut F).cast::<c_void>();
        let mut id = 0u64;
        check(unsafe { (self.h().on_frame)(self.me, tramp, user, &mut id) })?;
        self.owned.push(b);
        Ok(Id(id))
    }

    /// Serialized game-thread callback with mutable SDK access for dynamically
    /// registering owned resources. The host must not re-enter this mod while
    /// the callback runs. Existing `on_frame` remains sufficient for reads and
    /// requests that do not register new Rust closures.
    /// Releasing a registration stops delivery, but its Rust closure allocation
    /// remains owned by Cx until mod destruction, just like panel/on_frame.
    /// Avoid unbounded re-registration loops in long-lived mods.
    pub fn on_frame_mut(&mut self, f: impl FnMut(&mut Cx) + 'static) -> Result<Id> {
        struct F { cx: *mut Cx, f: Box<dyn FnMut(&mut Cx)> }
        unsafe extern "C" fn tramp(user: *mut c_void) {
            let s = &mut *(user as *mut F);
            // The callback box is separate from Cx and remains at a stable
            // address if registration grows Cx::owned. No shared Cx reference
            // is created here while the user's exclusive reference is live.
            let cx = &mut *s.cx;
            let f = &mut s.f;
            crate::entry::guard("mutable frame callback", || f(cx));
        }
        let mut b = Box::new(F { cx: self as *mut Cx, f: Box::new(f) });
        let user = (&mut *b as *mut F).cast::<c_void>();
        let mut id = 0;
        check(unsafe { (self.h().on_frame)(self.me, tramp, user, &mut id) })?;
        self.owned.push(b);
        Ok(Id(id))
    }

    /// Host-managed worker thread. `f` should return when `stop()` is true.
    pub fn spawn(&mut self, f: impl FnOnce(&dyn Fn() -> bool) + Send + 'static) -> Result<Id> {
        struct Job(Option<Box<dyn FnOnce(&dyn Fn() -> bool) + Send>>);
        unsafe extern "C" fn tramp(user: *mut c_void, stop: *const i32) {
            let job = &mut *(user as *mut Job);
            if let Some(f) = job.0.take() {
                let stop_fn = move || core::ptr::read_volatile(stop) != 0;
                crate::entry::guard("thread", || f(&stop_fn));
            }
        }
        let mut b = Box::new(Job(Some(Box::new(f))));
        let user = (&mut *b as *mut Job).cast::<c_void>();
        let mut id = 0u64;
        check(unsafe { (self.h().thread_spawn)(self.me, tramp, user, &mut id) })?;
        self.owned.push(b);
        Ok(Id(id))
    }

    /// Choose which `BBM_EV_*` events reach `Mod::event`.
    pub fn subscribe(&self, mask: u32) -> Result<()> {
        check(unsafe { (self.h().subscribe)(self.me, mask) })
    }

    // ---- Input (ABI 1.1; reads as released on an older host) ---------------

    /// Held this frame. Keys are SDL scancodes: see `bbmod::key`.
    pub fn key_down(&self, key: i32) -> bool {
        self.h().has_v1_1() && unsafe { (self.h().key_down)(self.me, key) != 0 }
    }
    /// Went down this frame (fires once per press).
    pub fn key_pressed(&self, key: i32) -> bool {
        self.h().has_v1_1() && unsafe { (self.h().key_pressed)(self.me, key) != 0 }
    }
    /// Pad buttons held (bits from `bbmod::pad`).
    pub fn pad_down(&self, buttons: u32) -> bool {
        self.h().has_v1_1() && unsafe { (self.h().pad_buttons)(self.me) } & buttons == buttons
    }
    /// All of `buttons` held and at least one of them went down this frame (combos fire once).
    pub fn pad_pressed(&self, buttons: u32) -> bool {
        if !self.h().has_v1_1() { return false; }
        let (held, new) = unsafe { ((self.h().pad_buttons)(self.me), (self.h().pad_pressed)(self.me)) };
        held & buttons == buttons && new & buttons != 0
    }

    // ---- UI ---------------------------------------------------------------

    /// Add a mod-owned section to a shared overlay window. Contributions are
    /// ordered by order, mod ID, then key; identical widget labels are isolated.
    /// This does not insert elements into the game's native menu system.
    pub fn contribute(&mut self, slot: &str, key: &str, order: i32, draw: impl FnMut(&Ui) + 'static) -> Result<Id> {
        if !self.h().has_ui_slots() { return Err(crate::Error::UNSUPPORTED); }
        struct P { f: Box<dyn FnMut(&Ui)>, ui: Ui }
        unsafe extern "C" fn tramp(user: *mut c_void) {
            let p = &mut *(user as *mut P);
            crate::entry::guard("UI contribution", || (p.f)(&p.ui));
        }
        let slot=cstr(slot)?; let key=cstr(key)?;
        let mut b=Box::new(P { f:Box::new(draw), ui:Ui::new(self.host,self.me,self as *const Cx) });
        let user=(&mut *b as *mut P).cast::<c_void>();
        let mut id=0;
        check(unsafe { (self.h().ui_contribute)(self.me,slot.as_ptr(),key.as_ptr(),order,tramp,user,&mut id) })?;
        self.owned.push(b);
        Ok(Id(id))
    }

    pub fn panel(&mut self, title: &str, draw: impl FnMut(&Ui) + 'static) -> Result<Id> {
        struct P {
            f: Box<dyn FnMut(&Ui)>,
            ui: Ui,
        }
        unsafe extern "C" fn tramp(user: *mut c_void) {
            let p = &mut *(user as *mut P);
            let ui = &p.ui;
            let f = &mut p.f;
            crate::entry::guard("panel", || f(ui));
        }
        let t = cstr(title)?;
        let mut b = Box::new(P { f: Box::new(draw), ui: Ui::new(self.host, self.me, self as *const Cx) });
        let user = (&mut *b as *mut P).cast::<c_void>();
        let mut id = 0u64;
        check(unsafe { (self.h().ui_panel)(self.me, t.as_ptr(), tramp, user, &mut id) })?;
        self.owned.push(b);
        Ok(Id(id))
    }

    pub fn notify(&self, msg: &str) {
        if let Ok(c) = cstr(msg) {
            unsafe { (self.h().ui_notify)(self.me, c.as_ptr()) }
        }
    }

    /// Raw Dear ImGui context for mods that link a matching ImGui.
    pub fn imgui_context(&self) -> *mut c_void { unsafe { (self.h().ui_imgui_context)(self.me) } }

    // ---- Files ------------------------------------------------------------

    /// Serve `source` when the game opens `game_path`
    /// (e.g. `"dvdroot_ps4/param/gameparam.parambnd.dcx"`).
    pub fn file_override(&self, game_path: &str, source: &str) -> Result<Id> {
        let g = cstr(game_path)?;
        let s = cstr(source)?;
        let mut id = 0u64;
        check(unsafe { (self.h().file_override)(self.me, g.as_ptr(), s.as_ptr(), &mut id) })?;
        Ok(Id(id))
    }

    // ---- ModNet -----------------------------------------------------------

    /// Opens a channel; `on_msg(cx, peer, bytes)` runs on the game thread at the sync point.
    pub fn channel(&mut self, name: &str, flags: u32, on_msg: impl FnMut(&Cx, u64, &[u8]) + 'static) -> Result<Channel> {
        struct N { cx: *const Cx, f: Box<dyn FnMut(&Cx, u64, &[u8])> }
        unsafe extern "C" fn tramp(user: *mut c_void, peer: u64, data: *const c_void, len: usize) {
            let n = &mut *(user as *mut N);
            let cx = &*n.cx;
            let f = &mut n.f;
            let s = if data.is_null() { &[][..] } else { core::slice::from_raw_parts(data.cast::<u8>(), len) };
            crate::entry::guard("net callback", || f(cx, peer, s));
        }
        let n = cstr(name)?;
        let mut id = 0u64;
        check(unsafe { (self.h().net_channel_open)(self.me, n.as_ptr(), flags, &mut id) })?;
        let mut b = Box::new(N { cx: self as *const Cx, f: Box::new(on_msg) });
        let user = (&mut *b as *mut N).cast::<c_void>();
        check(unsafe { (self.h().net_on_message)(self.me, id, tramp, user) })?;
        self.owned.push(b);
        Ok(Channel(id))
    }

    pub fn send(&self, ch: Channel, peer: u64, data: &[u8]) -> Result<()> {
        check(unsafe { (self.h().net_send)(self.me, ch.0, peer, data.as_ptr().cast(), data.len()) })
    }

    pub fn broadcast(&self, ch: Channel, data: &[u8]) -> Result<()> {
        check(unsafe { (self.h().net_broadcast)(self.me, ch.0, data.as_ptr().cast(), data.len()) })
    }

    pub fn session(&self) -> SessionInfo {
        let mut s = SessionInfo::default();
        unsafe { (self.h().session_info)(self.me, &mut s) };
        s
    }
}
