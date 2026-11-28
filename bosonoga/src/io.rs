//! Configured-directory I/O for Bosonoga programs.
//!
//! File operations take a `dir_*` setting and a single filename, or a filename
//! prefix and suffix for `io_latest`. Use `with ret INDEX_TYPE as NAME` to bind
//! whichever results the following block uses.
//!
//! All file operations return success at index 0 and an error message at index
//! 2. Index 1 is the text, name, timestamp, or (for create-only writes) whether
//! the filename was already taken. Reads and lookup also return true at index 3
//! when the file is missing. Timestamps are signed Unix nanoseconds.
//!
//! Text is UTF-8, and `str_len` and `str_digit` count and index its bytes, not
//! its characters, so `str_len "é"_str` is 2. `str_parse` takes digits with an
//! optional `-`, and no `+` or spaces.

use std::fs::{self, File};
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

use quick_xml::{Reader, events::Event};

use crate::value::{Kind, Value};

const TEXT_LIMIT: u64 = 1024 * 1024;
static NEXT_TEMP: AtomicU64 = AtomicU64::new(0);

pub trait Io {
    fn read(&mut self, directory: &Path, name: &str) -> io::Result<String>;
    fn write(&mut self, directory: &Path, name: &str, text: &str) -> io::Result<()>;
    fn modified(&mut self, directory: &Path, name: &str) -> io::Result<SystemTime>;
    fn latest(&mut self, directory: &Path, prefix: &str, suffix: &str) -> io::Result<String>;
}

pub struct FileIo;

fn regular_file(path: &Path) -> io::Result<fs::Metadata> {
    let metadata = fs::symlink_metadata(path)?;
    if metadata.is_file() {
        Ok(metadata)
    } else {
        Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "expected a regular file",
        ))
    }
}

struct Staging(PathBuf);

impl Staging {
    fn create(directory: &Path) -> io::Result<Self> {
        loop {
            let serial = NEXT_TEMP.fetch_add(1, Ordering::Relaxed);
            let path = directory.join(format!(".bosonoga-{}-{serial}", std::process::id()));
            match fs::create_dir(&path) {
                Ok(()) => return Ok(Self(path)),
                Err(error) if error.kind() == io::ErrorKind::AlreadyExists => {}
                Err(error) => return Err(error),
            }
        }
    }
}

impl Drop for Staging {
    fn drop(&mut self) {
        let _ = fs::remove_file(self.0.join("contents"));
        let _ = fs::remove_dir(&self.0);
    }
}

fn has_control_char(name: &str) -> bool {
    for c in name.chars() {
        if c.is_control() {
            return true;
        }
    }
    false
}

fn file_path(directory: &Path, name: &str) -> io::Result<PathBuf> {
    if name.is_empty()
        || name.len() > 255
        || name == "."
        || name == ".."
        || name.contains(['/', '\\', ':'])
        || has_control_char(name)
    {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "expected a single file name",
        ));
    }
    Ok(directory.join(name))
}

fn check_text(text: &str) -> io::Result<()> {
    let too_long = match u64::try_from(text.len()) {
        Ok(length) => length > TEXT_LIMIT,
        Err(_) => false,
    };
    if too_long || text.contains('\0') {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "expected UTF-8 text up to 1 MiB without NUL bytes",
        ));
    }
    Ok(())
}

impl Io for FileIo {
    fn read(&mut self, directory: &Path, name: &str) -> io::Result<String> {
        let path = file_path(directory, name)?;
        regular_file(&path)?;
        let file = File::open(path)?;
        let mut limited = file.take(TEXT_LIMIT + 1);
        let mut text = String::new();
        limited.read_to_string(&mut text)?;
        check_text(&text)?;
        Ok(text)
    }

    fn write(&mut self, directory: &Path, name: &str, text: &str) -> io::Result<()> {
        let path = file_path(directory, name)?;
        check_text(text)?;
        match fs::symlink_metadata(&path) {
            Ok(_) => return Err(io::ErrorKind::AlreadyExists.into()),
            Err(error) if error.kind() == io::ErrorKind::NotFound => {}
            Err(error) => return Err(error),
        }
        fs::create_dir_all(directory)?;
        let staging = Staging::create(directory)?;
        let staged = staging.0.join("contents");
        {
            let mut file = File::create_new(&staged)?;
            file.write_all(text.as_bytes())?;
            file.sync_all()?;
        }
        fs::hard_link(staged, path)
    }

    fn modified(&mut self, directory: &Path, name: &str) -> io::Result<SystemTime> {
        let path = file_path(directory, name)?;
        let metadata = regular_file(&path)?;
        metadata.modified()
    }

    fn latest(&mut self, directory: &Path, prefix: &str, suffix: &str) -> io::Result<String> {
        let mut latest: Option<(SystemTime, String)> = None;
        for found in fs::read_dir(directory)? {
            let entry = found?;
            let Ok(name) = entry.file_name().into_string() else {
                continue;
            };
            if !name.starts_with(prefix)
                || !name.ends_with(suffix)
                || file_path(directory, &name).is_err()
                || !entry.file_type()?.is_file()
            {
                continue;
            }
            let metadata = regular_file(&entry.path())?;
            let candidate = (metadata.modified()?, name);
            let is_newer = match &latest {
                Some(known) => candidate > *known,
                None => true,
            };
            if is_newer {
                latest = Some(candidate);
            }
        }
        match latest {
            Some((_, name)) => Ok(name),
            None => Err(io::ErrorKind::NotFound.into()),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Builtin {
    Read,
    Write,
    Modified,
    Latest,
    XmlChild,
    XmlAttribute,
    XmlName,
    XmlEscape,
    Parse,
    Digit,
    Length,
}

impl Builtin {
    pub(crate) fn named(word: &str) -> Option<Self> {
        Some(match word {
            "io_read" => Self::Read,
            "io_write" => Self::Write,
            "io_modified" => Self::Modified,
            "io_latest" => Self::Latest,
            "xml_child" => Self::XmlChild,
            "xml_attribute" => Self::XmlAttribute,
            "xml_name" => Self::XmlName,
            "xml_escape" => Self::XmlEscape,
            "str_parse" => Self::Parse,
            "str_digit" => Self::Digit,
            "str_len" => Self::Length,
            _ => return None,
        })
    }

    pub(crate) fn uses_directory(self) -> bool {
        matches!(
            self,
            Self::Read | Self::Write | Self::Modified | Self::Latest
        )
    }

    pub(crate) fn signature(self) -> (&'static [Kind], &'static [Kind]) {
        use Kind::{Bool, I32, I64, Str};
        match self {
            Self::Read => (&[Str], &[Bool, Str, Str, Bool]),
            Self::Write => (&[Str, Str], &[Bool, Bool, Str]),
            Self::Modified => (&[Str], &[Bool, I64, Str]),
            Self::Latest => (&[Str, Str], &[Bool, Str, Str, Bool]),
            Self::XmlChild => (&[Str, Str, I32], &[Bool, Str]),
            Self::XmlAttribute => (&[Str, Str], &[Bool, Str]),
            Self::XmlName | Self::XmlEscape => (&[Str], &[Bool, Str]),
            Self::Parse | Self::Length => (&[Str], &[Bool, I32]),
            Self::Digit => (&[Str, I32], &[Bool, I32]),
        }
    }

    // The names of the values of signature, which with ret takes, each
    // ending with its type.
    pub(crate) fn result_names(self) -> &'static [&'static str] {
        match self {
            Self::Read | Self::Latest => &["ok_bool", "text_str", "error_str", "missing_bool"],
            Self::Write => &["ok_bool", "taken_bool", "error_str"],
            Self::Modified => &["ok_bool", "nanos_i64", "error_str"],
            Self::XmlChild => &["found_bool", "child_str"],
            Self::XmlAttribute => &["found_bool", "value_str"],
            Self::XmlName => &["found_bool", "name_str"],
            Self::XmlEscape => &["ok_bool", "escaped_str"],
            Self::Parse => &["parsed_bool", "number_i32"],
            Self::Length => &["fits_bool", "len_i32"],
            Self::Digit => &["found_bool", "digit_i32"],
        }
    }

    pub(crate) fn evaluate(
        self,
        directory: Option<&Path>,
        values: &[Value],
        io: &mut dyn Io,
    ) -> Vec<Value> {
        use Value::{Bool, I32, I64, Str};
        match (self, directory, values) {
            (Self::Read, Some(dir), [Str(name)]) => {
                read_result(read_file(io, dir, name), dir, name)
            }
            (Self::Write, Some(dir), [Str(name), Str(text)]) => {
                let result = write_file(io, dir, name, text);
                let taken = has_error_kind(&result, io::ErrorKind::AlreadyExists);
                let written = match result {
                    Ok(()) => Ok(false),
                    Err(error) => Err(error),
                };
                let mut returned = file_result(written, dir, name, Bool);
                if let Some(slot) = returned.get_mut(1) {
                    *slot = Bool(taken);
                }
                returned
            }
            (Self::Modified, Some(dir), [Str(name)]) => {
                file_result(modified_nanos(io, dir, name), dir, name, I64)
            }
            (Self::Latest, Some(dir), [Str(prefix), Str(suffix)]) => {
                read_result(io.latest(dir, prefix, suffix), dir, "")
            }
            (Self::XmlChild, None, [Str(text), Str(name), I32(index)]) => {
                let mut found = None;
                if let Ok(row) = usize::try_from(*index) {
                    found = xml_value(text, XmlQuery::Child(name, row));
                }
                optional(found, Str)
            }
            (Self::XmlAttribute, None, [Str(text), Str(name)]) => {
                optional(xml_value(text, XmlQuery::Attribute(name)), Str)
            }
            (Self::XmlName, None, [Str(text)]) => optional(xml_value(text, XmlQuery::Name), Str),
            (Self::XmlEscape, None, [Str(text)]) => {
                let escaped = quick_xml::escape::escape(text);
                vec![Bool(true), Str(escaped.into_owned())]
            }
            (Self::Parse, None, [Str(text)]) => optional(parse_i32(text), I32),
            (Self::Length, None, [Str(text)]) => optional(i32::try_from(text.len()).ok(), I32),
            (Self::Digit, None, [Str(text), I32(index)]) => optional(digit_at(text, *index), I32),
            _ => unreachable!("the checker validates builtin arguments"),
        }
    }
}

fn read_file(io: &mut dyn Io, directory: &Path, name: &str) -> io::Result<String> {
    file_path(directory, name)?;
    let text = io.read(directory, name)?;
    check_text(&text)?;
    Ok(text)
}

fn write_file(io: &mut dyn Io, directory: &Path, name: &str, text: &str) -> io::Result<()> {
    file_path(directory, name)?;
    check_text(text)?;
    io.write(directory, name, text)
}

fn modified_nanos(io: &mut dyn Io, directory: &Path, name: &str) -> io::Result<i64> {
    file_path(directory, name)?;
    let time = io.modified(directory, name)?;
    let nanos = match time.duration_since(UNIX_EPOCH) {
        Ok(after_epoch) => i128::try_from(after_epoch.as_nanos()).map_err(io::Error::other)?,
        Err(before_epoch) => {
            let before = before_epoch.duration().as_nanos();
            -i128::try_from(before).map_err(io::Error::other)?
        }
    };
    i64::try_from(nanos).map_err(io::Error::other)
}

// An i32 written as digits with an optional -, so that each number has one
// form: Rust's parse also takes +7.
fn parse_i32(text: &str) -> Option<i32> {
    if text.starts_with('+') {
        return None;
    }
    text.parse().ok()
}

fn digit_at(text: &str, index: i32) -> Option<i32> {
    let at = usize::try_from(index).ok()?;
    let byte = *text.as_bytes().get(at)?;
    if !byte.is_ascii_digit() {
        return None;
    }
    Some(i32::from(byte - b'0'))
}

fn optional<T: Default>(value: Option<T>, wrap: fn(T) -> Value) -> Vec<Value> {
    vec![
        Value::Bool(value.is_some()),
        wrap(value.unwrap_or_default()),
    ]
}

fn has_error_kind<T>(result: &io::Result<T>, kind: io::ErrorKind) -> bool {
    match result {
        Ok(_) => false,
        Err(error) => error.kind() == kind,
    }
}

fn read_result(result: io::Result<String>, directory: &Path, name: &str) -> Vec<Value> {
    let missing = has_error_kind(&result, io::ErrorKind::NotFound);
    let mut values = file_result(result, directory, name, Value::Str);
    values.push(Value::Bool(missing));
    values
}

fn file_result<T: Default>(
    result: io::Result<T>,
    directory: &Path,
    name: &str,
    wrap: fn(T) -> Value,
) -> Vec<Value> {
    match result {
        Ok(value) => vec![Value::Bool(true), wrap(value), Value::Str(String::new())],
        Err(error) => vec![
            Value::Bool(false),
            wrap(T::default()),
            Value::Str(format!("{}: {error}", directory.join(name).display())),
        ],
    }
}

#[derive(Clone, Copy)]
enum XmlQuery<'a> {
    Name,
    Attribute(&'a str),
    Child(&'a str, usize),
}

// Scan the whole document so truncated or unbalanced input cannot yield a value.
fn xml_value(text: &str, query: XmlQuery<'_>) -> Option<String> {
    let mut reader = Reader::from_str(text);
    let mut depth = 0_usize;
    let mut root = false;
    let mut matches = 0;
    let mut child_start = None;
    let mut value = None;
    loop {
        let before = usize::try_from(reader.buffer_position()).ok()?;
        let event = reader.read_event().ok()?;
        let after = usize::try_from(reader.buffer_position()).ok()?;
        match event {
            Event::Start(ref tag) | Event::Empty(ref tag) => {
                if depth == 0 {
                    if root {
                        return None;
                    }
                    root = true;
                    if matches!(query, XmlQuery::Name) {
                        value = Some(tag.name().as_ref().to_owned());
                    }
                }
                for parsed in tag.attributes() {
                    let attribute = parsed.ok()?;
                    let decoded = attribute
                        .normalized_value(quick_xml::XmlVersion::Implicit1_0)
                        .ok()?;
                    if depth == 0
                        && let XmlQuery::Attribute(name) = query
                        && attribute.key.as_ref() == name
                    {
                        value = Some(decoded.into_owned());
                    }
                }
                if let XmlQuery::Child(name, index) = query
                    && depth == 1
                    && (name.is_empty() || tag.name().as_ref() == name)
                {
                    if index == matches {
                        if matches!(event, Event::Empty(_)) {
                            value = Some(text.get(before..after)?.to_owned());
                        } else {
                            child_start = Some(before);
                        }
                    }
                    matches += 1;
                }
                if let Event::Start(_) = event {
                    depth += 1;
                }
            }
            Event::End(_) => {
                depth = depth.checked_sub(1)?;
                if depth == 1
                    && let Some(start) = child_start.take()
                {
                    value = Some(text.get(start..after)?.to_owned());
                }
            }
            Event::Text(content) if depth == 0 => {
                if !is_blank(content.as_ref()) {
                    return None;
                }
            }
            Event::DocType(_) | Event::GeneralRef(_) | Event::CData(_) if depth == 0 => {
                return None;
            }
            Event::Decl(_) if before != 0 => return None,
            Event::Eof => {
                if root && depth == 0 {
                    return value;
                }
                return None;
            }
            Event::Text(_)
            | Event::CData(_)
            | Event::GeneralRef(_)
            | Event::Decl(_)
            | Event::PI(_)
            | Event::Comment(_) => {}
            Event::DocType(_) => return None,
        }
    }
}

fn is_blank(text: &str) -> bool {
    for byte in text.bytes() {
        if !byte.is_ascii_whitespace() {
            return false;
        }
    }
    true
}

#[cfg(test)]
mod tests {
    use super::{Builtin, FileIo, Io, Staging};
    use std::io::ErrorKind;

    #[test]
    fn result_names_end_with_their_types() {
        let words = [
            "io_read",
            "io_write",
            "io_modified",
            "io_latest",
            "xml_child",
            "xml_attribute",
            "xml_name",
            "xml_escape",
            "str_parse",
            "str_digit",
            "str_len",
        ];
        for word in words {
            let builtin = Builtin::named(word).unwrap();
            let (_, kinds) = builtin.signature();
            let names = builtin.result_names();
            assert_eq!(names.len(), kinds.len(), "{word}");
            for (name, kind) in names.iter().zip(kinds) {
                assert!(
                    name.ends_with(&format!("_{}", kind.name())),
                    "{word} {name}"
                );
            }
        }
    }

    #[test]
    fn file_io_publishes_once_and_reads_only_named_files() {
        let workspace = Staging::create(&std::env::temp_dir()).unwrap();
        let directory = &workspace.0;
        let mut files = FileIo;
        assert_eq!(
            files.latest(directory, "", "").unwrap_err().kind(),
            ErrorKind::NotFound
        );
        files.write(directory, "contents", "saved ✓").unwrap();
        assert_eq!(files.read(directory, "contents").unwrap(), "saved ✓");
        assert_eq!(files.latest(directory, "con", "ents").unwrap(), "contents");
        assert!(files.modified(directory, "contents").is_ok());
        assert_eq!(
            files
                .write(directory, "contents", "replacement")
                .unwrap_err()
                .kind(),
            ErrorKind::AlreadyExists
        );
        assert_eq!(files.read(directory, "contents").unwrap(), "saved ✓");
        assert_eq!(
            files.read(directory, "../contents").unwrap_err().kind(),
            ErrorKind::InvalidInput
        );
        assert_eq!(std::fs::read_dir(directory).unwrap().count(), 1);
    }
}
