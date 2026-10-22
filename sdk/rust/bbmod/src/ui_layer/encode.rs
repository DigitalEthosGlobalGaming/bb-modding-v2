//! The draw-list stream published to the host (`BBUI` v1, see `bbmod.h`). Stage
//! coordinates, little endian. The host validates it whole; `decode` is the SDK's
//! own reader for tests and tools.

use super::geom::Rect;
use super::node::{Align, Color, DrawCmd};

pub const MAX_BYTES: usize = 256 * 1024;
pub const MAX_COMMANDS: usize = 4096;

fn f(out: &mut Vec<u8>, v: f32) { out.extend_from_slice(&v.to_le_bytes()); }
fn rect(out: &mut Vec<u8>, r: Rect) { f(out, r.x); f(out, r.y); f(out, r.w); f(out, r.h); }

/// Encode as many commands as fit the host limits (extra commands are dropped, never
/// split). Text is clipped to 1024 bytes on a character boundary; control characters
/// are replaced by spaces; image names are limited to 255 bytes.
pub fn encode(cmds: &[DrawCmd]) -> Vec<u8> {
    let mut body = Vec::new();
    let mut count = 0u32;
    for c in cmds.iter().take(MAX_COMMANDS) {
        let before = body.len();
        match c {
            DrawCmd::Fill { rect: r, color } => { body.push(1); rect(&mut body, *r); body.extend_from_slice(&color.0.to_le_bytes()); }
            DrawCmd::Stroke { rect: r, color, width } => {
                body.push(2); rect(&mut body, *r); f(&mut body, *width); body.extend_from_slice(&color.0.to_le_bytes());
            }
            DrawCmd::Text { rect: r, text, size, color, align, bold } => {
                body.push(3); rect(&mut body, *r); f(&mut body, *size); body.extend_from_slice(&color.0.to_le_bytes());
                body.push(match align { Align::Start => 0, Align::Center => 1, Align::End => 2 });
                body.push(*bold as u8);
                let clean: String = text.chars().map(|c| if c.is_control() { ' ' } else { c }).collect();
                let mut end = clean.len().min(1024);
                while !clean.is_char_boundary(end) { end -= 1; }
                body.extend_from_slice(&(end as u16).to_le_bytes());
                body.extend_from_slice(&clean.as_bytes()[..end]);
            }
            DrawCmd::Image { rect: r, name, tint } => {
                let mut end = name.len().min(255);
                while !name.is_char_boundary(end) { end -= 1; }
                if end == 0 || name[..end].chars().any(|c| c.is_control()) { continue; }
                body.push(4); rect(&mut body, *r); body.extend_from_slice(&tint.0.to_le_bytes());
                body.push(end as u8); body.extend_from_slice(&name.as_bytes()[..end]);
            }
        }
        if 12 + body.len() > MAX_BYTES { body.truncate(before); break; }
        count += 1;
    }
    let mut out = Vec::with_capacity(12 + body.len());
    out.extend_from_slice(b"BBUI");
    out.extend_from_slice(&[1, 0, 0, 0]);
    out.extend_from_slice(&count.to_le_bytes());
    out.extend_from_slice(&body);
    out
}

/// Inverse of `encode`; None for anything the host would reject.
pub fn decode(b: &[u8]) -> Option<Vec<DrawCmd>> {
    if b.len() < 12 || &b[..4] != b"BBUI" || b[4] != 1 || b[5..8] != [0, 0, 0] || b.len() > MAX_BYTES { return None; }
    let count = u32::from_le_bytes(b[8..12].try_into().ok()?) as usize;
    if count > MAX_COMMANDS { return None; }
    let mut at = 12usize;
    let mut out = Vec::with_capacity(count);
    let rd_f = |at: &mut usize| -> Option<f32> {
        let v = f32::from_le_bytes(b.get(*at..*at + 4)?.try_into().ok()?);
        *at += 4;
        (v.is_finite() && v.abs() < 100000.0).then_some(v)
    };
    let rd_u32 = |at: &mut usize| -> Option<u32> {
        let v = u32::from_le_bytes(b.get(*at..*at + 4)?.try_into().ok()?);
        *at += 4;
        Some(v)
    };
    for _ in 0..count {
        let op = *b.get(at)?;
        at += 1;
        let r = Rect::new(rd_f(&mut at)?, rd_f(&mut at)?, rd_f(&mut at)?, rd_f(&mut at)?);
        out.push(match op {
            1 => DrawCmd::Fill { rect: r, color: Color(rd_u32(&mut at)?) },
            2 => { let w = rd_f(&mut at)?; DrawCmd::Stroke { rect: r, width: w, color: Color(rd_u32(&mut at)?) } }
            3 => {
                let size = rd_f(&mut at)?;
                if size <= 0.0 { return None; }
                let color = Color(rd_u32(&mut at)?);
                let align = match *b.get(at)? { 0 => Align::Start, 1 => Align::Center, 2 => Align::End, _ => return None };
                let bold = match *b.get(at + 1)? { 0 => false, 1 => true, _ => return None };
                let len = u16::from_le_bytes(b.get(at + 2..at + 4)?.try_into().ok()?) as usize;
                at += 4;
                let s = std::str::from_utf8(b.get(at..at + len)?).ok()?;
                at += len;
                if len > 1024 || s.chars().any(|c| c.is_control()) { return None; }
                DrawCmd::Text { rect: r, text: s.to_owned(), size, color, align, bold }
            }
            4 => {
                let tint = Color(rd_u32(&mut at)?);
                let len = *b.get(at)? as usize;
                at += 1;
                let s = std::str::from_utf8(b.get(at..at + len)?).ok()?;
                at += len;
                if len == 0 || s.chars().any(|c| c.is_control()) { return None; }
                DrawCmd::Image { rect: r, name: s.to_owned(), tint }
            }
            _ => return None,
        });
    }
    (at == b.len()).then_some(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    fn sample() -> Vec<DrawCmd> {
        vec![
            DrawCmd::Fill { rect: Rect::new(1.0, 2.0, 3.0, 4.0), color: Color(0x112233ff) },
            DrawCmd::Stroke { rect: Rect::new(0.0, 0.0, 10.0, 10.0), color: Color(0xa0824bff), width: 2.0 },
            DrawCmd::Text { rect: Rect::new(5.0, 5.0, 100.0, 36.0), text: "Mods \u{2713}".into(), size: 22.0, color: Color(0xa5a58cff), align: Align::End, bold: true },
            DrawCmd::Image { rect: Rect::new(0.0, 0.0, 32.0, 32.0), name: "KG_OK".into(), tint: Color(0xffffffff) },
        ]
    }
    #[test]
    fn roundtrips_and_matches_the_documented_header() {
        let bytes = encode(&sample());
        assert_eq!(&bytes[..8], b"BBUI\x01\0\0\0");
        assert_eq!(u32::from_le_bytes(bytes[8..12].try_into().unwrap()), 4);
        assert_eq!(decode(&bytes).unwrap(), sample());
    }
    #[test]
    fn host_rejects_corrupt_streams_and_the_encoder_sanitises() {
        let bytes = encode(&sample());
        assert!(decode(&bytes[..bytes.len() - 1]).is_none());
        let mut bad = bytes.clone(); bad[0] = b'X'; assert!(decode(&bad).is_none());
        let mut bad = bytes.clone(); bad[12] = 9; assert!(decode(&bad).is_none());
        let nasty = vec![DrawCmd::Text { rect: Rect::new(0.0, 0.0, 1.0, 1.0), text: "a\nb\u{0}c".into(), size: 20.0, color: Color(0), align: Align::Start, bold: false }];
        match &decode(&encode(&nasty)).unwrap()[0] { DrawCmd::Text { text, .. } => assert_eq!(text, "a b c"), _ => panic!() }
        let huge = vec![sample()[0].clone(); 10_000];
        let enc = encode(&huge);
        assert_eq!(decode(&enc).unwrap().len(), MAX_COMMANDS);
        let long = vec![DrawCmd::Text { rect: Rect::default(), text: "\u{e9}".repeat(2000), size: 20.0, color: Color(0), align: Align::Start, bold: false }];
        match &decode(&encode(&long)).unwrap()[0] { DrawCmd::Text { text, .. } => assert!(text.len() <= 1024 && text.chars().all(|c| c == '\u{e9}')), _ => panic!() }
    }
}
