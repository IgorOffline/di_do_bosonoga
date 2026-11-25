pub mod ast;
mod check;
pub mod gfx;
pub mod io;
pub mod key;
pub mod lega;
pub mod lexer;
mod module;
mod program;
pub mod setting;
mod source;
mod syntax_error;
pub mod value;

use std::path::Path;

use lalrpop_util::lalrpop_mod;

use ast::ExitStatus;
use lega::Lega;
use lexer::Lexer;
pub use program::Program;
use setting::Settings;
use source::{SourceFile, Sources};

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
        let mut line = 1;
        let mut column = 1;
        for byte in &source.as_bytes()[..end] {
            if *byte == b'\n' {
                line += 1;
                column = 1;
            } else {
                column += 1;
            }
        }
        format!("{path}:{line}:{column}: {}", self.message)
    }
}

pub fn parse(source: &str, lega: &Lega) -> Result<Program, Diagnostic> {
    let mut sources = Sources::default();
    sources.add(String::new(), source.to_owned());
    match compile(sources, lega, &Settings::default()) {
        Ok(program) => Ok(program),
        Err((_, diagnostic)) => Err(diagnostic),
    }
}

pub fn load(path: &Path, lega: &Lega, settings: &Settings) -> Result<Program, String> {
    let shown = path.display().to_string();
    let text = match std::fs::read_to_string(path) {
        Ok(text) => text,
        Err(error) => return Err(format!("{shown}: {}", io_message(&error))),
    };
    let mut sources = Sources::default();
    sources.add(shown, text);
    match compile(sources, lega, settings) {
        Ok(program) => Ok(program),
        Err((read, diagnostic)) => Err(read.located(&diagnostic)),
    }
}

pub fn run(program: &Program) -> Result<(String, ExitStatus), (String, Diagnostic)> {
    program.run()
}

// The step lega reads every file and checks its words, and only then does the
// next step parse and check the program.
fn compile(
    mut sources: Sources,
    lega: &Lega,
    settings: &Settings,
) -> Result<Program, (Sources, Diagnostic)> {
    let modules = match module::discover(&mut sources, lega) {
        Ok(modules) => modules,
        Err(diagnostic) => return Err((sources, diagnostic)),
    };
    match check_sources(&sources, settings, &modules) {
        Ok(program) => Ok(Program { sources, ..program }),
        Err(diagnostic) => Err((sources, diagnostic)),
    }
}

fn check_sources(
    sources: &Sources,
    settings: &Settings,
    modules: &[Vec<usize>],
) -> Result<Program, Diagnostic> {
    let mut files = Vec::new();
    for file in &sources.files {
        files.push(parse_file(file)?);
    }
    check::check(sources, settings, &files, modules)
}

fn parse_file(file: &SourceFile) -> Result<ast::File<'_>, Diagnostic> {
    let parser = grammar::FileParser::new();
    let lexer = Lexer::starting_at(&file.text, file.base, file.base);
    match parser.parse(lexer) {
        Ok(parsed) => Ok(parsed),
        Err(error) => Err(syntax_error::describe(file, error)),
    }
}

fn io_message(error: &std::io::Error) -> String {
    let message = error.to_string();
    match message.rfind(" (os error ") {
        Some(at) => message[..at].to_owned(),
        None => message,
    }
}

pub(crate) fn shown(text: &str) -> &str {
    let mut end = text.len().min(SHOWN_TOKEN_MAX);
    while !text.is_char_boundary(end) {
        end -= 1;
    }
    &text[..end]
}
