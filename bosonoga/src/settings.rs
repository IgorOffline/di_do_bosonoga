use std::path::{Path, PathBuf};

use crate::lexer::{Lexer, Tok};
use crate::value::Value;

const COLOR_HEX_LENGTH: usize = 6;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Asset {
    pub text: String,
    pub path: PathBuf,
}

#[derive(Clone, Debug, Default)]
pub struct Settings {
    pub name: String,
    pub version: String,
    pub gfx_width: u32,
    pub gfx_height: u32,
    pub background: u32,
    colors: Vec<(String, u32)>,
    globals: Vec<(String, Value)>,
    keys: Vec<(String, u32)>,
    fonts: Vec<(String, Asset)>,
    icons: Vec<(String, Asset)>,
    dirs: Vec<(String, Asset)>,
}

fn find<'a, T>(entries: &'a [(String, T)], key: &str) -> Option<&'a T> {
    entries
        .iter()
        .find(|(name, _)| name == key)
        .map(|(_, value)| value)
}

impl Settings {
    #[must_use]
    pub fn color(&self, key: &str) -> Option<u32> {
        find(&self.colors, key).copied()
    }

    #[must_use]
    pub fn global(&self, key: &str) -> Option<&Value> {
        find(&self.globals, key)
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
        let text = std::fs::read_to_string(path).map_err(|error| format!("{shown}: {error}"))?;
        let directory = path.parent().unwrap_or_else(|| Path::new(""));
        Settings::parse(&shown, directory, &text)
    }

    pub fn parse(path: &str, directory: &Path, contents: &str) -> Result<Settings, String> {
        let file = KeyFile::parse(contents).map_err(|error| format!("{path}: {error}"))?;
        let fail = |error: String| format!("{path}: {error}");
        let mut settings = Settings {
            name: file.text("meta", "name").map_err(fail)?,
            version: file.text("meta", "version").map_err(fail)?,
            ..Settings::default()
        };
        if file.has_group("gfx") {
            settings.gfx_width = file.positive(path, "width")?;
            settings.gfx_height = file.positive(path, "height")?;
        }
        for key in file.keys("color") {
            let text = file.text("color", key).map_err(fail)?;
            let valid = text.len() == COLOR_HEX_LENGTH
                && text
                    .bytes()
                    .all(|b| b.is_ascii_digit() || (b'A'..=b'F').contains(&b));
            let color = u32::from_str_radix(&text, 16)
                .ok()
                .filter(|_| valid)
                .ok_or_else(|| {
                    format!("{path}: {key} must be {COLOR_HEX_LENGTH} uppercase hex digits")
                })?;
            settings.colors.push((key.to_owned(), color));
        }
        if file.has_group("color") && file.has_group("gfx") {
            settings.background = settings
                .color("background")
                .ok_or_else(|| format!("{path}: the group color needs the key background"))?;
        }
        for key in file.keys("global") {
            let text = file.text("global", key).map_err(fail)?;
            let mut lexer = Lexer::new(&text);
            let value = match (lexer.next(), lexer.next()) {
                (Some(Ok((_, Tok::Literal(value), _))), Some(Ok((_, Tok::End, _))))
                    if value.kind().is_number() =>
                {
                    value
                }
                _ => {
                    return Err(format!(
                        "{path}: {key} must be one number, like 100_i32 or 100d0_f32"
                    ));
                }
            };
            settings.globals.push((key.to_owned(), value));
        }
        for key in file.keys("key") {
            let name = file.text("key", key).map_err(fail)?;
            let code = crate::key::named(&name).ok_or_else(|| {
                format!(
                    "{path}: {key} must name a key, like SAPP_KEYCODE_W, SAPP_KEYCODE_0 or SAPP_KEYCODE_ESCAPE"
                )
            })?;
            settings.keys.push((key.to_owned(), code));
        }
        let groups = [
            ("font", "a font file", "asset/font/IntelOneMono-Medium.ttf"),
            ("icon", "an icon file", "asset/icon/chevron.png"),
            ("dir", "a directory", "replay"),
        ];
        for (group, what, example) in groups {
            for key in file.keys(group) {
                let text = file.text(group, key).map_err(fail)?;
                if text.is_empty() {
                    return Err(format!(
                        "{path}: {key} must name {what}, like \"{example}\""
                    ));
                }
                let asset = Asset {
                    path: directory.join(&text),
                    text,
                };
                match group {
                    "font" => &mut settings.fonts,
                    "icon" => &mut settings.icons,
                    _ => &mut settings.dirs,
                }
                .push((key.to_owned(), asset));
            }
        }
        Ok(settings)
    }
}

struct KeyFile<'a> {
    groups: Vec<(&'a str, Vec<(&'a str, &'a str)>)>,
}

impl<'a> KeyFile<'a> {
    fn parse(text: &'a str) -> Result<KeyFile<'a>, String> {
        let mut groups: Vec<(&str, Vec<(&str, &str)>)> = Vec::new();
        for raw in text.lines() {
            let line = raw.trim_start();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            if let Some(header) = line.strip_prefix('[') {
                let name = header.trim_end().strip_suffix(']').unwrap_or(header);
                if !groups.iter().any(|(known, _)| *known == name) {
                    groups.push((name, Vec::new()));
                }
                continue;
            }
            let Some((left, right)) = line.split_once('=') else {
                return Err(format!(
                    "Key file contains line “{raw}” which is not a key-value pair, group, or comment"
                ));
            };
            let Some((_, entries)) = groups.last_mut() else {
                return Err(format!(
                    "Key file does not start with a group, but with the line “{raw}”"
                ));
            };
            let key = left.trim_end();
            let value = right.trim_start();
            if let Some(entry) = entries.iter_mut().find(|(known, _)| *known == key) {
                entry.1 = value;
            } else {
                entries.push((key, value));
            }
        }
        Ok(KeyFile { groups })
    }

    fn has_group(&self, group: &str) -> bool {
        self.groups.iter().any(|(name, _)| *name == group)
    }

    fn keys(&self, group: &str) -> Vec<&'a str> {
        self.groups
            .iter()
            .find(|(name, _)| *name == group)
            .map_or_else(Vec::new, |(_, entries)| {
                entries.iter().map(|(key, _)| *key).collect()
            })
    }

    fn raw(&self, group: &str, key: &str) -> Result<&'a str, String> {
        let (_, entries) = self
            .groups
            .iter()
            .find(|(name, _)| *name == group)
            .ok_or_else(|| format!("Key file does not have group “{group}”"))?;
        entries
            .iter()
            .find(|(known, _)| *known == key)
            .map(|(_, value)| *value)
            .ok_or_else(|| format!("Key file does not have key “{key}” in group “{group}”"))
    }

    fn text(&self, group: &str, key: &str) -> Result<String, String> {
        let value = self.raw(group, key)?;
        let unquoted = value
            .strip_prefix('"')
            .and_then(|inner| inner.strip_suffix('"'))
            .unwrap_or(value);
        Ok(unquoted.to_owned())
    }

    fn positive(&self, path: &str, key: &str) -> Result<u32, String> {
        let value = self
            .raw("gfx", key)
            .map_err(|error| format!("{path}: {error}"))?;
        let number: i64 = value
            .trim_end()
            .parse()
            .map_err(|_| format!("{path}: Value “{value}” cannot be interpreted as a number."))?;
        u32::try_from(number)
            .ok()
            .filter(|n| *n > 0)
            .ok_or_else(|| format!("{path}: {key} must be positive"))
    }
}
