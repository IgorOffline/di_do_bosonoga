use std::path::{Path, PathBuf};

use crate::lexer::{Lexer, Tok};
use crate::value::Value;

// RRGGBB, or RRGGBBAA with an alpha.
const COLOR_HEX_LENGTH: usize = 6;
const COLOR_ALPHA_HEX_LENGTH: usize = 8;
// The frames a second of a gfx app whose group gfx has no fps_max.
pub const DEFAULT_FPS_MAX: u32 = 60;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Asset {
    pub text: String,
    pub path: PathBuf,
}

/// The group mouse: how the pixels the mouse moved become
/// `gfx_mouse_move_x` and `gfx_mouse_move_y`. Each key is optional.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Mouse {
    /// What the pixels are multiplied by, 1 by default.
    pub sensitivity: f32,
    /// Whether moving the mouse north, up, is a negative `gfx_mouse_move_y`
    /// instead of a positive one, false by default.
    pub invert_north: bool,
    /// Whether moving the mouse west, left, is a positive `gfx_mouse_move_x`
    /// instead of a negative one, false by default.
    pub invert_west: bool,
}

impl Default for Mouse {
    fn default() -> Mouse {
        Mouse {
            sensitivity: 1.0,
            invert_north: false,
            invert_west: false,
        }
    }
}

impl Mouse {
    /// The move the program sees of the pixels the mouse moved.
    #[must_use]
    pub fn applied(&self, [x, y]: [f32; 2]) -> [f32; 2] {
        let west = if self.invert_west { -1.0 } else { 1.0 };
        let north = if self.invert_north { -1.0 } else { 1.0 };
        [x * self.sensitivity * west, y * self.sensitivity * north]
    }
}

#[derive(Clone, Debug, Default)]
pub struct Settings {
    pub name: String,
    pub version: String,
    /// Skip the host's version requirement check and startup diagnostics.
    pub skip_version_check: bool,
    pub gfx_width: u32,
    pub gfx_height: u32,
    /// The most frames a second the window shows, from the optional key
    /// `fps_max` of the group gfx, [`DEFAULT_FPS_MAX`] by default.
    pub fps_max: u32,
    pub background: u32,
    pub mouse: Mouse,
    colors: Vec<(String, u32)>,
    values: Vec<(String, Value)>,
    keys: Vec<(String, u32)>,
    fonts: Vec<(String, Asset)>,
    icons: Vec<(String, Asset)>,
    dirs: Vec<(String, Asset)>,
}

fn find<'a, T>(entries: &'a [(String, T)], key: &str) -> Option<&'a T> {
    for (name, value) in entries {
        if name == key {
            return Some(value);
        }
    }
    None
}

impl Settings {
    #[must_use]
    pub fn color(&self, key: &str) -> Option<u32> {
        find(&self.colors, key).copied()
    }

    #[must_use]
    pub fn value(&self, key: &str) -> Option<&Value> {
        find(&self.values, key)
    }

    #[must_use]
    pub fn key(&self, key: &str) -> Option<u32> {
        find(&self.keys, key).copied()
    }

    #[must_use]
    pub fn font(&self, key: &str) -> Option<&Asset> {
        find(&self.fonts, key)
    }

    #[must_use]
    pub fn icon(&self, key: &str) -> Option<&Asset> {
        find(&self.icons, key)
    }

    #[must_use]
    pub fn dir(&self, key: &str) -> Option<&Asset> {
        find(&self.dirs, key)
    }

    pub fn read(path: &Path) -> Result<Settings, String> {
        let shown = path.display().to_string();
        let text = match std::fs::read_to_string(path) {
            Ok(text) => text,
            Err(error) => return Err(format!("{shown}: {error}")),
        };
        let directory = match path.parent() {
            Some(directory) => directory,
            None => Path::new(""),
        };
        Settings::parse(&shown, directory, &text)
    }

    pub fn parse(path: &str, directory: &Path, contents: &str) -> Result<Settings, String> {
        match Settings::parse_groups(directory, contents) {
            Ok(settings) => Ok(settings),
            Err(error) => Err(format!("{path}: {error}")),
        }
    }

    fn parse_groups(directory: &Path, contents: &str) -> Result<Settings, String> {
        let file = KeyFile::parse(contents)?;
        let name = file.text("meta", "name")?;
        let version = file.text("meta", "version")?;
        let skip_version_check = file
            .raw("meta", "skip_version_check")
            .unwrap_or("false")
            .trim()
            .parse::<bool>()
            .map_err(|_| "skip_version_check must be true or false".to_owned())?;
        let mut settings = Settings {
            name,
            version,
            skip_version_check,
            fps_max: DEFAULT_FPS_MAX,
            ..Settings::default()
        };
        if file.has_group("gfx") {
            parse_gfx(&file, &mut settings)?;
        }
        for key in file.keys("color") {
            let text = file.text("color", key)?;
            let color = parse_color(key, &text)?;
            settings.colors.push((key.to_owned(), color));
        }
        if file.has_group("color") && file.has_group("gfx") {
            let Some(background) = settings.color("background") else {
                return Err("the group color needs the key background".to_owned());
            };
            if !crate::gfx::is_opaque(background) {
                return Err("background must be opaque, as RRGGBB".to_owned());
            }
            settings.background = background;
        }
        if file.has_group("mouse") {
            settings.mouse = parse_mouse(&file)?;
        }
        for key in file.keys("setting") {
            let text = file.text("setting", key)?;
            let value = parse_value(key, &text)?;
            settings.values.push((key.to_owned(), value));
        }
        for key in file.keys("key") {
            let key_name = file.text("key", key)?;
            let Some(code) = crate::key::named(&key_name) else {
                return Err(format!(
                    "{key} must name a key, like SAPP_KEYCODE_W, SAPP_KEYCODE_0 or SAPP_KEYCODE_ESCAPE"
                ));
            };
            settings.keys.push((key.to_owned(), code));
        }
        settings.fonts = parse_assets(
            &file,
            directory,
            "font",
            "a font file",
            "asset/font/IntelOneMono-Medium.ttf",
        )?;
        settings.icons = parse_assets(
            &file,
            directory,
            "icon",
            "an icon file",
            "asset/icon/chevron.png",
        )?;
        settings.dirs = parse_assets(&file, directory, "dir", "a directory", "replay")?;
        Ok(settings)
    }
}

fn is_upper_hex_digit(byte: u8) -> bool {
    matches!(byte, b'0'..=b'9' | b'A'..=b'F')
}

// A color as gfx keeps it: 0xTTRRGGBB, where TT is 255 less the alpha.
fn parse_color(key: &str, text: &str) -> Result<u32, String> {
    let error = format!(
        "{key} must be {COLOR_HEX_LENGTH} uppercase hex digits, RRGGBB, or {COLOR_ALPHA_HEX_LENGTH} with an alpha, RRGGBBAA"
    );
    if text.len() != COLOR_HEX_LENGTH && text.len() != COLOR_ALPHA_HEX_LENGTH {
        return Err(error);
    }
    for byte in text.bytes() {
        if !is_upper_hex_digit(byte) {
            return Err(error);
        }
    }
    let Ok(value) = u32::from_str_radix(text, 16) else {
        return Err(error);
    };
    if text.len() == COLOR_HEX_LENGTH {
        return Ok(value);
    }
    let [red, green, blue, alpha] = value.to_be_bytes();
    if alpha == u8::MAX {
        return Err(format!(
            "{key} has an alpha of FF, which is opaque, so it is written as RRGGBB: {}",
            text.get(..COLOR_HEX_LENGTH).unwrap_or_default()
        ));
    }
    Ok(u32::from_be_bytes([u8::MAX - alpha, red, green, blue]))
}

// The group gfx has the keys width and height, and optionally fps_max.
fn parse_gfx(file: &KeyFile<'_>, settings: &mut Settings) -> Result<(), String> {
    for key in file.keys("gfx") {
        if !matches!(key, "width" | "height" | "fps_max") {
            return Err(format!(
                "the group gfx has only the keys width, height and fps_max, not {key}"
            ));
        }
    }
    settings.gfx_width = file.positive("gfx", "width")?;
    settings.gfx_height = file.positive("gfx", "height")?;
    if file.raw("gfx", "fps_max").is_ok() {
        settings.fps_max = file.positive("gfx", "fps_max")?;
    }
    Ok(())
}

// One literal and nothing else, as a value in regina.ini.
fn one_literal(text: &str) -> Option<Value> {
    let mut lexer = Lexer::new(text);
    let first = lexer.next();
    let second = lexer.next();
    if let Some(Ok((_, Tok::Literal(value), _))) = first
        && let Some(Ok((_, Tok::End, _))) = second
    {
        return Some(value);
    }
    None
}

// The group mouse is of a gfx app, and has only the keys sensitivity,
// invert_north and invert_west, each optional.
fn parse_mouse(file: &KeyFile<'_>) -> Result<Mouse, String> {
    if !file.has_group("gfx") {
        return Err("the group mouse needs the group gfx".to_owned());
    }
    let mut mouse = Mouse::default();
    for key in file.keys("mouse") {
        let text = file.raw("mouse", key)?;
        match key {
            "sensitivity" => match one_literal(text) {
                Some(Value::F32(sensitivity)) if sensitivity.is_finite() && sensitivity > 0.0 => {
                    mouse.sensitivity = sensitivity;
                }
                _ => {
                    return Err(
                        "sensitivity must be one f32 number above 0, like 1d0_f32".to_owned()
                    );
                }
            },
            "invert_north" | "invert_west" => {
                let Some(Value::Bool(invert)) = one_literal(text) else {
                    return Err(format!("{key} must be true_bool or false_bool"));
                };
                if key == "invert_north" {
                    mouse.invert_north = invert;
                } else {
                    mouse.invert_west = invert;
                }
            }
            _ => {
                return Err(format!(
                    "the group mouse has only the keys sensitivity, invert_north and invert_west, not {key}"
                ));
            }
        }
    }
    Ok(mouse)
}

fn parse_value(key: &str, text: &str) -> Result<Value, String> {
    let mut lexer = Lexer::new(text);
    let first = lexer.next();
    let second = lexer.next();
    if let Some(Ok((_, Tok::Literal(value), _))) = first
        && let Some(Ok((_, Tok::End, _))) = second
        && value.kind().is_number()
    {
        return Ok(value);
    }
    Err(format!(
        "{key} must be one number, like 100_i32 or 100d0_f32"
    ))
}

fn parse_assets(
    file: &KeyFile<'_>,
    directory: &Path,
    group: &str,
    what: &str,
    example: &str,
) -> Result<Vec<(String, Asset)>, String> {
    let mut assets = Vec::new();
    for key in file.keys(group) {
        let text = file.text(group, key)?;
        if text.is_empty() {
            return Err(format!("{key} must name {what}, like \"{example}\""));
        }
        let path = directory.join(&text);
        assets.push((key.to_owned(), Asset { text, path }));
    }
    Ok(assets)
}

struct Group<'a> {
    name: &'a str,
    entries: Vec<(&'a str, &'a str)>,
}

impl<'a> Group<'a> {
    fn set(&mut self, key: &'a str, value: &'a str) {
        for entry in &mut self.entries {
            if entry.0 == key {
                entry.1 = value;
                return;
            }
        }
        self.entries.push((key, value));
    }
}

struct KeyFile<'a> {
    groups: Vec<Group<'a>>,
}

impl<'a> KeyFile<'a> {
    fn parse(text: &'a str) -> Result<KeyFile<'a>, String> {
        let mut file = KeyFile { groups: Vec::new() };
        for raw in text.lines() {
            let line = raw.trim_start();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            if let Some(header) = line.strip_prefix('[') {
                let name = match header.trim_end().strip_suffix(']') {
                    Some(name) => name,
                    None => header,
                };
                if !file.has_group(name) {
                    file.groups.push(Group {
                        name,
                        entries: Vec::new(),
                    });
                }
                continue;
            }
            let Some((left, right)) = line.split_once('=') else {
                return Err(format!(
                    "Key file contains line “{raw}” which is not a key-value pair, group, or comment"
                ));
            };
            let Some(group) = file.groups.last_mut() else {
                return Err(format!(
                    "Key file does not start with a group, but with the line “{raw}”"
                ));
            };
            group.set(left.trim_end(), right.trim_start());
        }
        Ok(file)
    }

    fn group(&self, name: &str) -> Option<&Group<'a>> {
        for group in &self.groups {
            if group.name == name {
                return Some(group);
            }
        }
        None
    }

    fn has_group(&self, name: &str) -> bool {
        self.group(name).is_some()
    }

    fn keys(&self, name: &str) -> Vec<&'a str> {
        let mut keys = Vec::new();
        if let Some(group) = self.group(name) {
            for (key, _) in &group.entries {
                keys.push(*key);
            }
        }
        keys
    }

    fn raw(&self, name: &str, key: &str) -> Result<&'a str, String> {
        let Some(group) = self.group(name) else {
            return Err(format!("Key file does not have group “{name}”"));
        };
        for (known, value) in &group.entries {
            if *known == key {
                return Ok(value);
            }
        }
        Err(format!(
            "Key file does not have key “{key}” in group “{name}”"
        ))
    }

    fn text(&self, group: &str, key: &str) -> Result<String, String> {
        let value = self.raw(group, key)?;
        if let Some(inner) = value.strip_prefix('"')
            && let Some(unquoted) = inner.strip_suffix('"')
        {
            return Ok(unquoted.to_owned());
        }
        Ok(value.to_owned())
    }

    fn positive(&self, group: &str, key: &str) -> Result<u32, String> {
        let value = self.raw(group, key)?;
        let Ok(number) = value.trim_end().parse::<i64>() else {
            return Err(format!(
                "Value “{value}” cannot be interpreted as a number."
            ));
        };
        if let Ok(positive) = u32::try_from(number)
            && positive > 0
        {
            return Ok(positive);
        }
        Err(format!("{key} must be positive"))
    }
}
