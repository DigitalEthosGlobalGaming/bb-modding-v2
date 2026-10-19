//! Test mod: `detour!` on the test function at eboot+0x200 (returns x + 5).
//! The detour doubles the result, and panics for x == 13 to prove the panic
//! fallback calls the original instead of crashing.
use bbmod::{Cx, Mod, Result};

bbmod::detour! {
    fn add_five(x: i32) -> i32 {
        if x == 13 { panic!("unlucky"); }
        original(x) * 2
    }
}

struct HookMod;

impl Mod for HookMod {
    fn load(cx: &mut Cx) -> Result<Self> {
        add_five::install(cx, cx.eboot_base() + 0x200)?;
        Ok(HookMod)
    }
}

bbmod::export!(HookMod);
