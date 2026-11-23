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

#[cfg(test)]
mod tests {
    use super::{PNG_SIGNATURE, check_icon, metrics, png_size};

    // A font of only a table directory, a head table and a hhea table.
    fn font(tables: &[([u8; 4], Vec<u8>)]) -> Vec<u8> {
        let count = u16::try_from(tables.len()).unwrap();
        let mut bytes = vec![0, 1, 0, 0];
        bytes.extend_from_slice(&count.to_be_bytes());
        bytes.extend_from_slice(&[0; 6]);
        let mut offset = 12 + 16 * tables.len();
        for (tag, data) in tables {
            bytes.extend_from_slice(tag);
            bytes.extend_from_slice(&[0; 4]);
            bytes.extend_from_slice(&u32::try_from(offset).unwrap().to_be_bytes());
            bytes.extend_from_slice(&u32::try_from(data.len()).unwrap().to_be_bytes());
            offset += data.len();
        }
        for (_, data) in tables {
            bytes.extend_from_slice(data);
        }
        bytes
    }

    fn head(units: u16) -> Vec<u8> {
        let mut data = vec![0; 54];
        data[18..20].copy_from_slice(&units.to_be_bytes());
        data
    }

    fn hhea(ascender: i16, descender: i16) -> Vec<u8> {
        let mut data = vec![0; 36];
        data[4..6].copy_from_slice(&ascender.to_be_bytes());
        data[6..8].copy_from_slice(&descender.to_be_bytes());
        data
    }

    fn png(width: u32, height: u32) -> Vec<u8> {
        let mut bytes = PNG_SIGNATURE.to_vec();
        bytes.extend_from_slice(&13_u32.to_be_bytes());
        bytes.extend_from_slice(b"IHDR");
        bytes.extend_from_slice(&width.to_be_bytes());
        bytes.extend_from_slice(&height.to_be_bytes());
        bytes
    }

    #[test]
    fn metrics_of_a_font() {
        let bytes = font(&[(*b"head", head(2048)), (*b"hhea", hhea(1900, -500))]);
        assert_eq!(metrics(&bytes), Some((2048.0, 1900.0, -500.0)));
    }

    #[test]
    fn metrics_in_any_table_order() {
        let bytes = font(&[(*b"hhea", hhea(800, -200)), (*b"head", head(1000))]);
        assert_eq!(metrics(&bytes), Some((1000.0, 800.0, -200.0)));
    }

    #[test]
    fn metrics_reject_a_broken_font() {
        assert_eq!(metrics(&[]), None);
        assert_eq!(metrics(b"not a font at all"), None);
        assert_eq!(metrics(&font(&[(*b"head", head(2048))])), None);
        assert_eq!(metrics(&font(&[(*b"hhea", hhea(1900, -500))])), None);
        let no_units = font(&[(*b"head", head(0)), (*b"hhea", hhea(1900, -500))]);
        assert_eq!(metrics(&no_units), None);
        let upside_down = font(&[(*b"head", head(2048)), (*b"hhea", hhea(-500, 1900))]);
        assert_eq!(metrics(&upside_down), None);
        let mut truncated = font(&[(*b"head", head(2048)), (*b"hhea", hhea(1900, -500))]);
        truncated.truncate(truncated.len() - 30);
        assert_eq!(metrics(&truncated), None);
    }

    #[test]
    fn size_of_a_png() {
        assert_eq!(png_size(&png(36, 24)), Some((36, 24)));
        assert_eq!(png_size(&png(36, 24)[..23]), None);
        let mut wrong = png(36, 24);
        wrong[1] = b'J';
        assert_eq!(png_size(&wrong), None);
        let mut no_header = png(36, 24);
        no_header[12..16].copy_from_slice(b"IDAT");
        assert_eq!(png_size(&no_header), None);
    }

    #[test]
    fn icons_up_to_1024_a_side() {
        assert_eq!(check_icon("i", &png(1024, 1024)), Ok(()));
        assert_eq!(
            check_icon("i", &png(1025, 1)),
            Err("i is 1025 by 1 pixels, more than 1024 a side".to_owned())
        );
        assert_eq!(
            check_icon("i", &png(1, 2000)),
            Err("i is 1 by 2000 pixels, more than 1024 a side".to_owned())
        );
        assert_eq!(
            check_icon("i", b"GIF89a"),
            Err("i is not a PNG image".to_owned())
        );
    }
}
