pub mod ast;
mod check;
pub mod lexer;
mod program;
pub mod value;

use lalrpop_util::{ParseError, lalrpop_mod};

use ast::ExitStatus;
use lexer::{Lexer, Tok};
pub use program::Program;

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
    let main = grammar::MainParser::new()
        .parse(Lexer::new(source))
        .map_err(|error| syntax_error(source, error))?;
    check::check(source, &main)
}

pub fn run(program: &Program) -> Result<(String, ExitStatus), (String, Diagnostic)> {
    program.run()
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
            let has = |terminal: &str| expected.iter().any(|e| e.trim_matches('"') == terminal);
            if has("is") && matches!(token, Tok::Name(_)) {
                let declaration = match tokens_before(source, start).iter().rev().nth(1) {
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
                format!("'{}'", shown(&source[start..end]))
            };
            let wanted = if has("info") {
                "a statement".to_owned()
            } else if has("compare") {
                let after_bool = matches!(
                    tokens_before(source, start).last(),
                    Some(Tok::Literal(value::Value::Bool(_)))
                );
                if after_bool { "di" } else { "a comparison" }.to_owned()
            } else if has("di") {
                "di".to_owned()
            } else if has("literal") {
                "a value".to_owned()
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

fn tokens_before(source: &str, offset: usize) -> Vec<Tok<'_>> {
    Lexer::new(source)
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
