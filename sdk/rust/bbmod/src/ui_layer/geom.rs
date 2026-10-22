//! Stage geometry. UI is authored on the game's 1920x1080 stage and scaled
//! uniformly into the same aspect-fit rectangle the port uses for the game frame.

pub const STAGE_W: f32 = 1920.0;
pub const STAGE_H: f32 = 1080.0;

#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct Rect { pub x: f32, pub y: f32, pub w: f32, pub h: f32 }

impl Rect {
    pub const STAGE: Rect = Rect::new(0.0, 0.0, STAGE_W, STAGE_H);
    pub const fn new(x: f32, y: f32, w: f32, h: f32) -> Self { Self { x, y, w, h } }
    pub fn right(&self) -> f32 { self.x + self.w }
    pub fn bottom(&self) -> f32 { self.y + self.h }
    pub fn inset(&self, d: f32) -> Rect {
        Rect::new(self.x + d, self.y + d, (self.w - 2.0 * d).max(0.0), (self.h - 2.0 * d).max(0.0))
    }
}

/// Maps stage coordinates to output pixels.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Canvas { pub origin: (f32, f32), pub scale: f32 }

impl Canvas {
    /// `frame` is the game frame size; `out` is the swapchain size. The frame is
    /// aspect-fit and centred in `out` (as the presenter's blit does), then the
    /// stage is aspect-fit and centred inside that rectangle. None for invalid sizes.
    pub fn fit(out: (f32, f32), frame: (f32, f32)) -> Option<Self> {
        let ok = |v: f32| v.is_finite() && v > 0.0;
        if !(ok(out.0) && ok(out.1) && ok(frame.0) && ok(frame.1)) { return None; }
        let s = (out.0 / frame.0).min(out.1 / frame.1);
        let (fw, fh) = (frame.0 * s, frame.1 * s);
        let (fx, fy) = ((out.0 - fw) / 2.0, (out.1 - fh) / 2.0);
        let scale = (fw / STAGE_W).min(fh / STAGE_H);
        Some(Self { origin: (fx + (fw - STAGE_W * scale) / 2.0, fy + (fh - STAGE_H * scale) / 2.0), scale })
    }
    pub fn to_screen(&self, r: Rect) -> Rect {
        Rect::new(self.origin.0 + r.x * self.scale, self.origin.1 + r.y * self.scale, r.w * self.scale, r.h * self.scale)
    }
    pub fn to_stage(&self, x: f32, y: f32) -> (f32, f32) {
        ((x - self.origin.0) / self.scale, (y - self.origin.1) / self.scale)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn scales_and_centres_on_any_output() {
        let c = Canvas::fit((1920.0, 1080.0), (1920.0, 1080.0)).unwrap();
        assert_eq!((c.origin, c.scale), ((0.0, 0.0), 1.0));
        let c = Canvas::fit((3840.0, 1080.0), (1920.0, 1080.0)).unwrap(); // ultrawide: pillarbox
        assert_eq!((c.origin, c.scale), ((960.0, 0.0), 1.0));
        let c = Canvas::fit((1280.0, 1024.0), (1920.0, 1080.0)).unwrap(); // tall: letterbox
        assert!((c.scale - 2.0 / 3.0).abs() < 1e-6 && c.origin.0 == 0.0 && c.origin.1 > 0.0);
        let r = c.to_screen(Rect::STAGE);
        assert!((r.w - 1280.0).abs() < 1e-3);
        let (x, y) = c.to_stage(c.origin.0 + 300.0 * c.scale, c.origin.1 + 90.0 * c.scale);
        assert!((x - 300.0).abs() < 1e-3 && (y - 90.0).abs() < 1e-3);
        assert!(Canvas::fit((0.0, 1.0), (1.0, 1.0)).is_none() && Canvas::fit((1.0, 1.0), (f32::NAN, 1.0)).is_none());
    }
}
