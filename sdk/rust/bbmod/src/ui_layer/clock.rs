//! Fixed 60 Hz UI clock and presence animation.
//!
//! Native menu timelines are authored at 30 fps. UI state advances in exact
//! 1/60 s ticks regardless of the present rate, so behaviour is identical at 60,
//! 120 or 144 Hz. Authored frame counts keep their native duration: one authored
//! frame is two ticks.

pub const TICK_HZ: u64 = 60;
pub const TICKS_PER_AUTHORED_FRAME: u64 = 2;
pub const MAX_CATCH_UP: u32 = 4;

#[derive(Debug, Default, Clone)]
pub struct UiClock { last_us: Option<u64>, acc: u64, ticks: u64 }

impl UiClock {
    pub fn new() -> Self { Self::default() }
    pub fn ticks(&self) -> u64 { self.ticks }
    /// Advance to a monotonic time in microseconds; returns ticks elapsed (capped
    /// at MAX_CATCH_UP, dropping the excess after a hitch). A backwards time is ignored.
    pub fn advance(&mut self, now_us: u64) -> u32 {
        let now = now_us.max(self.last_us.unwrap_or(0));
        let Some(last) = self.last_us.replace(now) else { return 0 };
        self.acc += (now - last) * TICK_HZ; // exact: one tick per 1_000_000 units
        let due = self.acc / 1_000_000;
        self.acc %= 1_000_000;
        let n = due.min(MAX_CATCH_UP as u64) as u32;
        self.ticks += n as u64;
        n
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Playback {
    /// Interpolate between authored frames: smooth at 60 Hz, same total duration.
    Smooth,
    /// Hold each authored frame for two ticks, as the native 30 fps menus do.
    Stepped,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Phase { Hidden, FadingIn, Shown, FadingOut }

/// Visibility with the native FadeIn / Loop / FadeOut shape. Defaults are
/// 6 authored frames each way (the common value measured in the game's movies).
#[derive(Clone, Copy, Debug)]
pub struct Presence {
    pub in_frames: u32,
    pub out_frames: u32,
    pub playback: Playback,
    phase: Phase,
    start_tick: u64,
    from: f32,
}

impl Default for Presence {
    fn default() -> Self { Self::new(6, 6, Playback::Smooth) }
}

impl Presence {
    pub fn new(in_frames: u32, out_frames: u32, playback: Playback) -> Self {
        Self { in_frames, out_frames, playback, phase: Phase::Hidden, start_tick: 0, from: 0.0 }
    }
    pub fn phase(&self) -> Phase { self.phase }
    pub fn is_drawn(&self) -> bool { self.phase != Phase::Hidden }
    pub fn wants_visible(&self) -> bool { matches!(self.phase, Phase::FadingIn | Phase::Shown) }

    fn progress(&self, tick: u64, frames: u32) -> f32 {
        if frames == 0 { return 1.0; }
        let elapsed = tick.saturating_sub(self.start_tick);
        let t = match self.playback {
            Playback::Smooth => elapsed as f32 / (frames as u64 * TICKS_PER_AUTHORED_FRAME) as f32,
            Playback::Stepped => (elapsed / TICKS_PER_AUTHORED_FRAME) as f32 / frames as f32,
        };
        t.clamp(0.0, 1.0)
    }
    pub fn alpha(&self, tick: u64) -> f32 {
        match self.phase {
            Phase::Hidden => 0.0,
            Phase::Shown => 1.0,
            Phase::FadingIn => self.from + (1.0 - self.from) * self.progress(tick, self.in_frames),
            Phase::FadingOut => self.from * (1.0 - self.progress(tick, self.out_frames)),
        }
    }
    pub fn show(&mut self, tick: u64) {
        if self.wants_visible() { return; }
        self.from = self.alpha(tick);
        self.phase = Phase::FadingIn;
        self.start_tick = tick;
    }
    pub fn hide(&mut self, tick: u64) {
        if matches!(self.phase, Phase::Hidden | Phase::FadingOut) { return; }
        self.from = self.alpha(tick);
        self.phase = Phase::FadingOut;
        self.start_tick = tick;
    }
    /// Settle finished fades. Call once per tick (or before reading `phase`).
    pub fn update(&mut self, tick: u64) {
        match self.phase {
            Phase::FadingIn if self.progress(tick, self.in_frames) >= 1.0 => self.phase = Phase::Shown,
            Phase::FadingOut if self.progress(tick, self.out_frames) >= 1.0 => self.phase = Phase::Hidden,
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn ticks_are_exact_independent_of_present_rate() {
        for hz in [60u64, 120, 144, 59] {
            let mut c = UiClock::new();
            let step = 1_000_000 / hz;
            c.advance(0);
            let mut t = 0;
            while t < 10_000_000 { t += step; c.advance(t); }
            let expect = t * 60 / 1_000_000;
            assert!(c.ticks().abs_diff(expect) <= 1, "{hz} Hz: {} vs {expect}", c.ticks());
        }
    }
    #[test]
    fn hitch_is_capped_and_time_going_backwards_is_ignored() {
        let mut c = UiClock::new();
        c.advance(1_000_000);
        assert_eq!(c.advance(5_000_000), MAX_CATCH_UP);
        assert_eq!(c.advance(4_000_000), 0);
        assert_eq!(c.advance(5_000_000), 0);
    }
    #[test]
    fn fade_keeps_native_duration_in_both_playback_modes() {
        for mode in [Playback::Smooth, Playback::Stepped] {
            let mut p = Presence::new(6, 6, mode);
            p.show(100);
            assert_eq!(p.alpha(100), 0.0);
            assert!(p.alpha(106) > 0.0 && p.alpha(106) < 1.0);
            p.update(111); assert_eq!(p.phase(), Phase::FadingIn);
            p.update(112); assert_eq!((p.phase(), p.alpha(112)), (Phase::Shown, 1.0));
            p.hide(200);
            p.update(212); assert_eq!((p.phase(), p.alpha(212)), (Phase::Hidden, 0.0));
        }
        let mut p = Presence::new(6, 6, Playback::Stepped);
        p.show(0);
        assert_eq!(p.alpha(0), p.alpha(1)); // held for the whole authored frame
        assert!(p.alpha(2) > p.alpha(1));
    }
    #[test]
    fn interrupting_a_fade_starts_from_the_current_alpha() {
        let mut p = Presence::default();
        p.show(0);
        let a = p.alpha(6);
        p.hide(6);
        assert!((p.alpha(6) - a).abs() < 1e-6);
        assert!(p.alpha(9) < a);
    }
}
