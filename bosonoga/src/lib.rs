pub mod ast;
mod check;
pub mod gfx;
pub mod key;
pub mod lexer;
mod program;
pub mod settings;
mod sources;
pub mod value;

use std::path::Path;

use lalrpop_util::{ParseError, lalrpop_mod};

use ast::ExitStatus;
use lexer::{Lexer, Tok};
pub use program::Program;
use settings::Settings;
use sources::{SourceFile, Sources};

lalrpop_mod!(
    #[allow(
        unreachable_pub,
        unused_qualifications,
        unused_lifetimes,
        clippy::all,
        clippy::pedantic,
        clippy::restriction
    )]
    grammar
);

pub const VERSION: &str = env!("CARGO_PKG_VERSION");

const SHOWN_TOKEN_MAX: usize = 32;
const FILE_SUFFIX: &str = ".bosonoga";
const EXPECTED_FILE_SUFFIX: &str = ".xp.bosonoga";

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Diagnostic {
    pub offset: usize,
    pub message: String,
}

impl Diagnostic {
    pub fn new(offset: usize, message: impl Into<String>) -> Self {
        Diagnostic {
            offset,
            message: message.into(),
        }
    }

    #[must_use]
    pub fn located(&self, path: &str, source: &str) -> String {
        let end = self.offset.min(source.len());
        let mut lines = source.as_bytes()[..end].split(|&b| b == b'\n');
        let line = lines.clone().count();
        let column = 1 + lines.next_back().map_or(0, <[u8]>::len);
        format!("{path}:{line}:{column}: {}", self.message)
    }
}

pub fn parse(source: &str) -> Result<Program, Diagnostic> {
    let mut sources = Sources::default();
    sources.add(String::new(), source.to_owned());
    compile(sources, &Settings::default()).map_err(|(_, diagnostic)| diagnostic)
}

pub fn load(path: &Path, settings: &Settings) -> Result<Program, String> {
    let shown = path.display().to_string();
    let text = std::fs::read_to_string(path)
        .map_err(|error| format!("{shown}: {}", io_message(&error)))?;
    let mut sources = Sources::default();
    sources.add(shown, text);
    compile(sources, settings).map_err(|(read, diagnostic)| read.located(&diagnostic))
}

pub fn run(program: &Program) -> Result<(String, ExitStatus), (String, Diagnostic)> {
    program.run()
}

fn compile(mut sources: Sources, settings: &Settings) -> Result<Program, (Sources, Diagnostic)> {
    let modules = match discover(&mut sources) {
        Ok(modules) => modules,
        Err(diagnostic) => return Err((sources, diagnostic)),
    };
    let checked = sources
        .files
        .iter()
        .map(parse_file)
        .collect::<Result<Vec<_>, _>>()
        .and_then(|files| check::check(&sources, settings, &files, &modules));
    match checked {
        Ok(program) => Ok(Program { sources, ..program }),
        Err(diagnostic) => Err((sources, diagnostic)),
    }
}

fn io_message(error: &std::io::Error) -> String {
    let message = error.to_string();
    match message.rfind(" (os error ") {
        Some(at) => message[..at].to_owned(),
        None => message,
    }
}

fn is_import_name(name: &str) -> bool {
    name.len() > FILE_SUFFIX.len()
        && !name.starts_with('.')
        && name.ends_with(FILE_SUFFIX)
        && !name.ends_with(EXPECTED_FILE_SUFFIX)
        && !name.contains(['/', '\\'])
}

fn discover(sources: &mut Sources) -> Result<Vec<Vec<usize>>, Diagnostic> {
    let main_path = Path::new(sources.path()).to_path_buf();
    let directory = main_path.parent().unwrap_or_else(|| Path::new(""));
    let program_name = main_path
        .file_name()
        .map_or_else(String::new, |name| name.to_string_lossy().into_owned());
    let mut modules = Vec::new();
    let mut read_by = vec![None];
    let mut index = 0;
    while let Some(file) = sources.files.get(index) {
        let parsed = parse_file(file)?;
        if let (true, Some((span, name))) = (
            parsed.items.is_empty(),
            read_by.get(index).cloned().flatten(),
        ) {
            return Err(Diagnostic::new(
                span,
                format!("{name} has no items, and a module has at least one"),
            ));
        }
        let names: Vec<(usize, String)> = parsed
            .items
            .iter()
            .filter_map(|item| match item {
                ast::Item::Mod(span, name) => Some((span.start, name.clone())),
                ast::Item::Const { .. }
                | ast::Item::Def { .. }
                | ast::Item::Array { .. }
                | ast::Item::Section(..) => None,
            })
            .collect();
        let mut indices = Vec::new();
        for (at, name) in names {
            if !is_import_name(&name) {
                return Err(Diagnostic::new(
                    at,
                    "mod takes the name of a .bosonoga file in the directory of the program, as in mod \"lorem.bosonoga\"_str",
                ));
            }
            if name == program_name {
                return Err(Diagnostic::new(at, "a program cannot read itself with mod"));
            }
            let already = sources.files.iter().skip(1).any(|known| {
                Path::new(&known.path)
                    .file_name()
                    .is_some_and(|known_name| known_name.to_string_lossy() == name)
            });
            if already {
                return Err(Diagnostic::new(
                    at,
                    format!("{name} is already read, with mod or io_raw_import"),
                ));
            }
            let path = directory.join(&name).display().to_string();
            let text = std::fs::read_to_string(&path).map_err(|error| {
                Diagnostic::new(at, format!("cannot read {path}: {}", io_message(&error)))
            })?;
            indices.push(sources.add(path, text));
            read_by.push(Some((at, name)));
        }
        modules.push(indices);
        index += 1;
    }
    Ok(modules)
}

fn parse_file(file: &SourceFile) -> Result<ast::File<'_>, Diagnostic> {
    grammar::FileParser::new()
        .parse(Lexer::starting_at(&file.text, file.base, file.base))
        .map_err(|error| syntax_error(file, error))
}

fn syntax_error(file: &SourceFile, error: ParseError<usize, Tok<'_>, Diagnostic>) -> Diagnostic {
    match error {
        ParseError::User { error: user } => user,
        ParseError::UnrecognizedToken {
            token: (start, token, end),
            expected,
        } => {
            let has = |terminal: &str| expected.iter().any(|e| e.trim_matches('"') == terminal);
            let before = tokens_before(file, start);
            if has("is") && matches!(token, Tok::Name(_)) {
                let declaration = match before.iter().rev().nth(1) {
                    Some(Tok::Val) => "val",
                    _ => "var",
                };
                return Diagnostic::new(
                    start,
                    format!(
                        "a {declaration} declares one variable, and the values of a call are taken with call ... with ret, one per line"
                    ),
                );
            }
            let got = if token == Tok::End {
                "end of file".to_owned()
            } else {
                format!(
                    "'{}'",
                    shown(
                        file.text
                            .get(start - file.base..end - file.base)
                            .unwrap_or("")
                    )
                )
            };
            let wanted = if has("section") && has("end") {
                "const, def, val, bosonoga_global, bosonoga_main, gfx_init, gfx_frame, gfx_cleanup, gfx_key_down or gfx_main".to_owned()
            } else if has("section") {
                "a program, which has a bosonoga_main block".to_owned()
            } else if has("info") {
                "a statement".to_owned()
            } else if has("compare") {
                let after_bool = matches!(before.last(), Some(Tok::Literal(value::Value::Bool(_))));
                if after_bool { "di" } else { "a comparison" }.to_owned()
            } else if has("di") && !has("literal") && !has("indexed_type") {
                "di".to_owned()
            } else if has("literal") && !has("indexed_type") {
                "a value".to_owned()
            } else if has("indexed_type") {
                "a type with its index, like 0_i32, of i32, i64, u32, u64, f32, f64, flags, color, str or bool".to_owned()
            } else {
                listed(&expected)
            };
            Diagnostic::new(start, format!("expected {wanted}, got {got}"))
        }
        ParseError::UnrecognizedEof { location, .. }
        | ParseError::InvalidToken { location }
        | ParseError::ExtraToken {
            token: (location, ..),
        } => Diagnostic::new(location, "unexpected token"),
    }
}

fn tokens_before(file: &SourceFile, offset: usize) -> Vec<Tok<'_>> {
    Lexer::starting_at(&file.text, file.base, file.base)
        .map_while(Result::ok)
        .take_while(|(start, ..)| *start < offset)
        .map(|(_, token, _)| token)
        .collect()
}

fn listed(expected: &[String]) -> String {
    let words: Vec<&str> = expected
        .iter()
        .map(|e| match e.trim_matches('"') {
            "end" => "end of file",
            "name" => "a variable name",
            "operation" => {
                "to, to_add, to_subtract, to_multiply, to_divide, to_mod or a conversion"
            }
            word => word,
        })
        .collect();
    match words.split_last() {
        Some((last, [])) => (*last).to_owned(),
        Some((last, rest)) => format!("{} or {last}", rest.join(", ")),
        None => "nothing".to_owned(),
    }
}

pub(crate) fn shown(text: &str) -> &str {
    let mut end = text.len().min(SHOWN_TOKEN_MAX);
    while !text.is_char_boundary(end) {
        end -= 1;
    }
    &text[..end]
}
