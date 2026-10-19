use core::ffi::c_void;
use core::sync::atomic::{AtomicI32, AtomicPtr, Ordering};

/// Holds the trampoline (callable original) for one hook. Declare as a `static`:
///
/// ```ignore
/// static DAMAGE: bbmod::Detour = bbmod::Detour::new();
/// extern "sysv64" fn my_damage(this: *mut c_void, amount: i32) -> i32 {
///     let _g = bbmod::InflightGuard::enter();
///     let orig: extern "sysv64" fn(*mut c_void, i32) -> i32 = unsafe { DAMAGE.original() };
///     orig(this, amount / 2)
/// }
/// ```
///
/// A `static` holding only a function pointer is fine: it lives in your DLL and
/// disappears with it on reload.
pub struct Detour {
    tramp: AtomicPtr<c_void>,
}

impl Detour {
    pub const fn new() -> Detour { Detour { tramp: AtomicPtr::new(core::ptr::null_mut()) } }
    pub(crate) fn set(&self, p: *mut c_void) { self.tramp.store(p, Ordering::Release) }
    pub fn trampoline(&self) -> *mut c_void { self.tramp.load(Ordering::Acquire) }

    /// The original function as a callable fn pointer type `F`.
    ///
    /// # Safety
    /// `F` must be a function pointer type matching the hooked function.
    pub unsafe fn original<F: Copy>(&self) -> F {
        assert_eq!(core::mem::size_of::<F>(), core::mem::size_of::<*mut c_void>());
        let p = self.trampoline();
        core::mem::transmute_copy::<*mut c_void, F>(&p)
    }
}

impl Default for Detour { fn default() -> Self { Self::new() } }

/// Set by the SDK at load, cleared at unload: where this mod's in-flight counter lives.
pub(crate) static INFLIGHT: AtomicPtr<AtomicI32> = AtomicPtr::new(core::ptr::null_mut());

/// Put one at the top of every detour. While it lives the host knows game threads
/// are inside your code, so unload waits for them instead of unmapping the DLL.
pub struct InflightGuard(*mut AtomicI32);

impl InflightGuard {
    #[inline]
    pub fn enter() -> InflightGuard {
        let p = INFLIGHT.load(Ordering::Acquire);
        if !p.is_null() { unsafe { (*p).fetch_add(1, Ordering::AcqRel) }; }
        InflightGuard(p)
    }
}

impl Drop for InflightGuard {
    #[inline]
    fn drop(&mut self) {
        if !self.0.is_null() { unsafe { (*self.0).fetch_sub(1, Ordering::AcqRel) }; }
    }
}

/// Declares a hook in one place: the detour, its trampoline and an installer.
///
/// ```ignore
/// bbmod::detour! {
///     /// Halve all damage the player takes.
///     fn apply_damage(this: *mut c_void, amount: i32) -> i32 {
///         original(this, amount / 2)
///     }
/// }
/// // in load():  apply_damage::install(cx, cx.ps4(0x0123_4567))?;
/// ```
///
/// Inside the body `original` is the game's function. The macro adds the
/// in-flight guard, and catches panics: a panicking body logs once and falls
/// back to calling `original` with the same arguments, so the game keeps going.
/// Arguments must be `Copy` (raw pointers, integers, floats), as FFI types are.
#[macro_export]
macro_rules! detour {
    ($(#[$m:meta])* $vis:vis fn $name:ident($($arg:ident : $ty:ty),* $(,)?) $(-> $ret:ty)? $body:block) => {
        #[allow(non_snake_case, dead_code)]
        $vis mod $name {
            #[allow(unused_imports)]
            use super::*;
            /// Trampoline storage for this hook.
            pub static DETOUR: $crate::Detour = $crate::Detour::new();
            /// The hooked function's type (System V, as PS4 code uses).
            pub type Fn = extern "sysv64" fn($($ty),*) $(-> $ret)?;

            /// Calls the game's original function (through the trampoline).
            #[allow(dead_code)]
            pub fn original($($arg: $ty),*) $(-> $ret)? {
                let f: Fn = unsafe { DETOUR.original::<Fn>() };
                f($($arg),*)
            }

            $(#[$m])*
            pub extern "sysv64" fn detour($($arg: $ty),*) $(-> $ret)? {
                let _guard = $crate::InflightGuard::enter();
                match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(move || $body)) {
                    Ok(v) => v,
                    Err(_) => {
                        $crate::__detour_panicked(stringify!($name));
                        original($($arg),*)
                    }
                }
            }

            /// Inline-hooks the function at `target` (a host address, e.g. `cx.ps4(..)`).
            pub fn install(cx: &$crate::Cx, target: usize) -> $crate::Result<$crate::Id> {
                unsafe { cx.hook_inline(target, detour as *mut ::core::ffi::c_void, &DETOUR) }
            }

            /// Hooks a function-pointer slot (vtable entry) instead.
            pub fn install_slot(cx: &$crate::Cx, slot: usize) -> $crate::Result<$crate::Id> {
                unsafe { cx.hook_slot(slot as *mut *mut ::core::ffi::c_void, detour as *mut ::core::ffi::c_void, &DETOUR) }
            }
        }
    };
}

#[doc(hidden)]
pub fn __detour_panicked(name: &str) {
    use core::sync::atomic::AtomicBool;
    static ONCE: AtomicBool = AtomicBool::new(false);
    if !ONCE.swap(true, Ordering::Relaxed) {
        eprintln!("[bbmod] detour {name} panicked; calling the original instead (logged once)");
    }
}
