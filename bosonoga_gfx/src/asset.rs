use macroquad::prelude::ImageFormat;
use macroquad::text::{Font, load_ttf_font_from_bytes};
use macroquad::texture::{Image, Texture2D};

const ICON_SIDE_MAX: u32 = 1024;
const PNG_SIGNATURE: [u8; 8] = [0x89, b'P', b'N', b'G', b'\r', b'\n', 0x1A, b'\n'];

pub(crate) struct Typeface {
    pub(crate) font: Font,
    // Of the em, which fontdue sizes by, to the height from the descender to
    // the ascender, which fontstash sizes by.
    pub(crate) em_per_height: f32,
    pub(crate) ascender: f32,
    pub(crate) descender: f32,
}

pub(crate) struct Assets {
    pub(crate) typefaces: Vec<Typeface>,
    pub(crate) icons: Vec<Texture2D>,
}

// The key, the shown path and the bytes of each font or icon file.
pub(crate) type Files = [(String, String, Vec<u8>)];

fn read_u16(bytes: &[u8], at: usize) -> Option<u16> {
    Some(u16::from_be_bytes([*bytes.get(at)?, *bytes.get(at + 1)?]))
}

fn read_u32(bytes: &[u8], at: usize) -> Option<u32> {
    Some(u32::from_be_bytes([
        *bytes.get(at)?,
        *bytes.get(at + 1)?,
        *bytes.get(at + 2)?,
        *bytes.get(at + 3)?,
    ]))
}

fn table(bytes: &[u8], tag: [u8; 4]) -> Option<usize> {
    let count = usize::from(read_u16(bytes, 4)?);
    for index in 0..count {
        let entry = 12 + 16 * index;
        if bytes.get(entry..entry + 4) == Some(tag.as_slice()) {
            let offset = read_u32(bytes, entry + 8)?;
            return usize::try_from(offset).ok();
        }
    }
    None
}

// The units per em of head, and the ascender and descender of hhea.
fn metrics(bytes: &[u8]) -> Option<(f32, f32, f32)> {
    let head = table(bytes, *b"head")?;
    let hhea = table(bytes, *b"hhea")?;
    let units = f32::from(read_u16(bytes, head + 18)?);
    let ascender = f32::from(i16::from_be_bytes(read_u16(bytes, hhea + 4)?.to_be_bytes()));
    let descender = f32::from(i16::from_be_bytes(read_u16(bytes, hhea + 6)?.to_be_bytes()));
    if units <= 0.0 || ascender <= descender {
        return None;
    }
    Some((units, ascender, descender))
}

// The width and the height of the header of a PNG, which is checked first so
// that a large one is not decoded.
fn png_size(bytes: &[u8]) -> Option<(u32, u32)> {
    if bytes.get(..8)? != PNG_SIGNATURE.as_slice() || bytes.get(12..16)? != b"IHDR".as_slice() {
        return None;
    }
    let width = read_u32(bytes, 16)?;
    let height = read_u32(bytes, 20)?;
    Some((width, height))
}

// What is checked before an icon is decoded.
fn check_icon(prefix: &str, bytes: &[u8]) -> Result<(), String> {
    let Some((width, height)) = png_size(bytes) else {
        return Err(format!("{prefix} is not a PNG image"));
    };
    if width > ICON_SIDE_MAX || height > ICON_SIDE_MAX {
        return Err(format!(
            "{prefix} is {width} by {height} pixels, more than {ICON_SIDE_MAX} a side"
        ));
    }
    Ok(())
}

// Only its alpha is kept, over white, so that the color of gfx_draw_icon tints
// it without dark edges where it is filtered.
fn icon(program: &str, key: &str, path: &str, bytes: &[u8]) -> Result<Texture2D, String> {
    let prefix = format!("{program}: icon_{key}: {path}");
    check_icon(&prefix, bytes)?;
    let mut image = match Image::from_file_with_format(bytes, Some(ImageFormat::Png)) {
        Ok(image) => image,
        Err(error) => return Err(format!("{prefix} is not a PNG image: {error}")),
    };
    let (pixels, _) = image.bytes.as_chunks_mut::<4>();
    for pixel in pixels {
        pixel[0] = u8::MAX;
        pixel[1] = u8::MAX;
        pixel[2] = u8::MAX;
    }
    Ok(Texture2D::from_image(&image))
}

impl Assets {
    pub(crate) fn load(program: &str, fonts: &Files, icons: &Files) -> Result<Assets, String> {
        let mut typefaces = Vec::new();
        for (key, path, bytes) in fonts {
            let not_truetype = format!("{program}: font_{key}: {path} is not a TrueType font");
            let Some((units, ascender, descender)) = metrics(bytes) else {
                return Err(not_truetype);
            };
            let Ok(font) = load_ttf_font_from_bytes(bytes) else {
                return Err(not_truetype);
            };
            let height = ascender - descender;
            typefaces.push(Typeface {
                font,
                em_per_height: units / height,
                ascender: ascender / height,
                descender: descender / height,
            });
        }
        let mut textures = Vec::new();
        for (key, path, bytes) in icons {
            textures.push(icon(program, key, path, bytes)?);
        }
        Ok(Assets {
            typefaces,
            icons: textures,
        })
    }
}
