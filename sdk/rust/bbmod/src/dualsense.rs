//! SDL-proven DualSense effect builders. Raw effects require hardware validation.
use crate::{check, sys, Cx, Error, Result};
pub type PadInfo = sys::bbm_pad_info;
pub const HAS_TRIGGERS: u32 = 1;
pub const HAS_LIGHTBAR: u32 = 2;
pub const CAPTURED: u32 = 4;
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum Trigger {
    Left = 0,
    Right = 1,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Effect(pub [u8; 11]);
pub fn off() -> Effect {
    let mut b = [0; 11];
    b[0] = 5;
    Effect(b)
}
pub fn resistance(start: u8, force: u8) -> Effect {
    let mut b = [0; 11];
    b[0] = 1;
    b[1] = start;
    b[2] = force;
    Effect(b)
}
/// Parameters are raw SDL reference bytes; their physical meaning is unverified.
pub fn resistance_vibration(a: u8, b: u8, c: u8) -> Effect {
    let mut out = [0; 11];
    out[..4].copy_from_slice(&[6, a, b, c]);
    Effect(out)
}
pub fn raw(bytes: [u8; 11]) -> Effect {
    Effect(bytes)
}
impl Cx {
    fn pad_field(&self, end: usize) -> Result<()> {
        let size = unsafe { core::ptr::addr_of!((*self.host).size).read() } as usize;
        if size >= end { Ok(()) } else { Err(Error::UNSUPPORTED) }
    }
    pub fn pad_info(&self) -> Result<PadInfo> {
        self.pad_field(core::mem::offset_of!(sys::bbm_host, pad_info) + core::mem::size_of::<usize>())?;
        let mut out = PadInfo::default();
        check(unsafe { (core::ptr::addr_of!((*self.host).pad_info).read())(self.me, &mut out) })?;
        Ok(out)
    }
    pub fn pad_trigger(&self, which: Trigger, effect: Effect) -> Result<()> {
        self.pad_field(core::mem::offset_of!(sys::bbm_host, pad_trigger) + core::mem::size_of::<usize>())?;
        check(unsafe { (core::ptr::addr_of!((*self.host).pad_trigger).read())(self.me, which as u32, effect.0.as_ptr()) })
    }
    pub fn pad_release(&self, which: Trigger) -> Result<()> {
        self.pad_field(core::mem::offset_of!(sys::bbm_host, pad_release) + core::mem::size_of::<usize>())?;
        check(unsafe { (core::ptr::addr_of!((*self.host).pad_release).read())(self.me, which as u32) })
    }
    pub fn pad_lightbar(&self, rgb: [u8; 3]) -> Result<()> {
        self.pad_field(core::mem::offset_of!(sys::bbm_host, pad_lightbar) + core::mem::size_of::<usize>())?;
        check(unsafe { (core::ptr::addr_of!((*self.host).pad_lightbar).read())(self.me, rgb[0], rgb[1], rgb[2]) })
    }
    /// Replace the current finite grip waveform. 48 kHz stereo, at most 250 ms,
    /// samples in [-0.5,0.5]. Empty data stops and releases the host-owned stream.
    /// USB only; unsupported on older hosts or without a suitable audio endpoint.
    pub fn pad_haptics(&self, stereo: &[[f32; 2]]) -> Result<()> {
        if stereo.len() > 12000 || stereo.iter().flatten().any(|x| !x.is_finite() || x.abs() > 0.5) {
            return Err(Error::ARG);
        }
        self.pad_field(core::mem::offset_of!(sys::bbm_host, pad_haptics) + core::mem::size_of::<usize>())?;
        check(unsafe { (core::ptr::addr_of!((*self.host).pad_haptics).read())(self.me, stereo.as_ptr().cast(), stereo.len() as u32) })
    }

}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn reference_bytes() {
        assert_eq!(off().0, [5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
        assert_eq!(resistance(0, 110).0, [1, 0, 110, 0, 0, 0, 0, 0, 0, 0, 0]);
        assert_eq!(
            resistance_vibration(15, 63, 128).0,
            [6, 15, 63, 128, 0, 0, 0, 0, 0, 0, 0]
        );
    }
    #[test]
    fn older_host_keeps_triggers_but_rejects_haptics() {
        // An actual truncated ABI table: do not form a full bbm_host reference.
        let end = core::mem::offset_of!(sys::bbm_host, pad_info) + core::mem::size_of::<usize>();
        let mut storage = vec![0usize; end / core::mem::size_of::<usize>()];
        let host = storage.as_mut_ptr().cast::<sys::bbm_host>();
        unsafe extern "C" fn info(_: sys::bbm_handle, out: *mut PadInfo) -> i32 {
            unsafe { out.write(PadInfo::default()); } 0
        }
        unsafe {
            core::ptr::addr_of_mut!((*host).size).write(end as u32);
            core::ptr::addr_of_mut!((*host).pad_info).write(info);
        }
        let cx = Cx::new(host, core::ptr::null_mut());
        assert!(cx.pad_info().is_ok());
        assert_eq!(cx.pad_haptics(&[]), Err(Error::UNSUPPORTED));
        assert_eq!(cx.pad_haptics(&[[f32::NAN,0.0]]), Err(Error::ARG));
        assert_eq!(cx.pad_haptics(&vec![[0.0;2];12001]), Err(Error::ARG));
    }
    #[test]
    fn c_layout() {
        assert_eq!(std::mem::size_of::<PadInfo>(), 24);
    }
}
