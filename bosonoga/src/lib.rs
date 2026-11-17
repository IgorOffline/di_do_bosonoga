pub mod ast;
pub mod lexer;

use std::fmt::Write as _;

use lalrpop_util::{ParseError, lalrpop_mod};

use ast::{ExitStatus, Program, Statement};
use lexer::{Lexer, Tok};

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
        let mut lines = source.as_bytes()[..self.offset].split(|&b| b == b'\n');
        let line = lines.clone().count();
        let column = 1 + lines.next_back().map_or(0, <[u8]>::len);
        format!("{path}:{line}:{column}: {}", self.message)
    }
}

pub fn parse(source: &str) -> Result<Program, Diagnostic> {
    grammar::ProgramParser::new()
        .parse(Lexer::new(source))
        .map_err(|error| syntax_error(source, error))
}

fn syntax_error(source: &str, error: ParseError<usize, Tok<'_>, Diagnostic>) -> Diagnostic {
    match error {
        ParseError::User { error: user } => user,
        ParseError::UnrecognizedToken {
            token: (start, token, end),
            expected,
        } => {
            if token == Tok::End && expected == ["\"bosonoga_main\""] {
                return Diagnostic::new(start, "a program needs a bosonoga_main block");
            }
            let got = if token == Tok::End {
                "end of file".to_owned()
            } else {
                format!("'{}'", shown(&source[start..end]))
            };
            Diagnostic::new(
                start,
                format!("expected {}, got {got}", expected_words(&expected)),
            )
        }
        ParseError::UnrecognizedEof { location, .. }
        | ParseError::InvalidToken { location }
        | ParseError::ExtraToken {
            token: (location, ..),
        } => Diagnostic::new(location, "unexpected token"),
    }
}

fn shown(text: &str) -> &str {
    let mut end = text.len().min(SHOWN_TOKEN_MAX);
    while !text.is_char_boundary(end) {
        end -= 1;
    }
    &text[..end]
}

fn expected_words(expected: &[String]) -> String {
    let has = |terminal: &str| expected.iter().any(|e| e.trim_matches('"') == terminal);
    if has("info") {
        return "a statement".to_owned();
    }
    if has("number") {
        return "a value".to_owned();
    }
    let words: Vec<&str> = expected
        .iter()
        .map(|e| match e.trim_matches('"') {
            "end" => "end of file",
            word => word,
        })
        .collect();
    match words.split_last() {
        Some((last, [])) => (*last).to_owned(),
        Some((last, rest)) => format!("{} or {last}", rest.join(", ")),
        None => "nothing".to_owned(),
    }
}

pub fn run(program: &Program) -> Result<(String, ExitStatus), (String, Diagnostic)> {
    let mut output = String::new();
    for statement in &program.statements {
        match statement {
            Statement::Info(values) => {
                for value in values {
                    let _ = write!(output, "{value}");
                }
                output.push('\n');
            }
            Statement::Nihil => {}
            Statement::Return(status) => return Ok((output, *status)),
        }
    }
    Err((
        output,
        Diagnostic::new(program.end, "the program ended without a return"),
    ))
}
