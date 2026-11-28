use lalrpop_util::ParseError;

use crate::Diagnostic;
use crate::lexer::{Lexer, Tok};
use crate::shown;
use crate::source::SourceFile;
use crate::value::Value;

pub(crate) fn describe(
    file: &SourceFile,
    error: ParseError<usize, Tok<'_>, Diagnostic>,
) -> Diagnostic {
    match error {
        ParseError::User { error: user } => user,
        ParseError::UnrecognizedToken {
            token: (start, token, end),
            expected,
        } => unrecognized(file, start, &token, end, &expected),
        ParseError::UnrecognizedEof { location, .. }
        | ParseError::InvalidToken { location }
        | ParseError::ExtraToken {
            token: (location, ..),
        } => Diagnostic::new(location, "unexpected token"),
    }
}

fn unrecognized(
    file: &SourceFile,
    start: usize,
    token: &Tok<'_>,
    end: usize,
    expected: &[String],
) -> Diagnostic {
    let before = tokens_before(file, start);
    if has(expected, "is") && !has(expected, "except") && matches!(token, Tok::Name(_)) {
        let mut declaration = "var";
        if before.len() >= 2 && before[before.len() - 2] == Tok::Val {
            declaration = "val";
        }
        return Diagnostic::new(
            start,
            format!(
                "a {declaration} declares one variable, and the values of a call are taken with call ... with ret, one per line"
            ),
        );
    }
    let got = if *token == Tok::End {
        "end of file".to_owned()
    } else {
        format!("'{}'", shown(file.slice(start, end)))
    };
    let wanted = wanted(expected, &before);
    Diagnostic::new(start, format!("expected {wanted}, got {got}"))
}

fn wanted(expected: &[String], before: &[Tok<'_>]) -> String {
    if has(expected, "section") && has(expected, "end") {
        // After the values of an aka_i32 that can also be followed by except.
        let except = if has(expected, "except") {
            "except, "
        } else {
            ""
        };
        return format!(
            "{except}aka_i32, const, def, val, global, main, gfx_init, gfx_frame, gfx_cleanup, gfx_key_down, gfx_key_up or gfx_main"
        );
    }
    if has(expected, "section") {
        return "a program, which has a main block".to_owned();
    }
    if has(expected, "info") {
        return "a statement".to_owned();
    }
    if has(expected, "compare") {
        if let Some(Tok::Literal(Value::Bool(_))) = before.last() {
            return "di".to_owned();
        }
        return "a comparison".to_owned();
    }
    let literal = has(expected, "literal");
    let indexed_type = has(expected, "indexed_type");
    // After with ret 0_bool as found, which another taken value or the block follows.
    if has(expected, "di")
        && indexed_type
        && let [.., Tok::As, Tok::Name(_)] = before
    {
        return "another value to take, like 1_i32 as count, or di".to_owned();
    }
    if has(expected, "di") && !literal && !indexed_type {
        return "di".to_owned();
    }
    if literal && !indexed_type {
        return "a value".to_owned();
    }
    if indexed_type {
        return "a type with its index, like 0_i32, of i32, i64, u32, u64, f32, f64, flags, color, str or bool".to_owned();
    }
    listed(expected)
}

fn has(expected: &[String], terminal: &str) -> bool {
    for name in expected {
        if name.trim_matches('"') == terminal {
            return true;
        }
    }
    false
}

fn tokens_before(file: &SourceFile, offset: usize) -> Vec<Tok<'_>> {
    let mut tokens = Vec::new();
    for spanned in Lexer::starting_at(&file.text, file.base, file.base) {
        let Ok((start, token, _)) = spanned else {
            break;
        };
        if start >= offset {
            break;
        }
        tokens.push(token);
    }
    tokens
}

fn terminal_word(terminal: &str) -> &str {
    match terminal {
        "end" => "end of file",
        "name" => "a variable name",
        "gfx_align_x" => "gfx_align_x_left, gfx_align_x_center or gfx_align_x_right",
        "gfx_align_y" => {
            "gfx_align_y_top, gfx_align_y_middle, gfx_align_y_baseline or gfx_align_y_bottom"
        }
        "operation" => "to, to_add, to_subtract, to_multiply, to_divide, to_mod or a conversion",
        word => word,
    }
}

fn listed(expected: &[String]) -> String {
    // The words of an operation have to among them.
    let mut terminals = Vec::new();
    for terminal in expected {
        if terminal != "\"to\"" || !has(expected, "operation") {
            terminals.push(terminal);
        }
    }
    if terminals.is_empty() {
        return "nothing".to_owned();
    }
    let last = terminals.len() - 1;
    let mut text = String::new();
    for (index, terminal) in terminals.iter().enumerate() {
        if index > 0 && index == last {
            text.push_str(" or ");
        } else if index > 0 {
            text.push_str(", ");
        }
        text.push_str(terminal_word(terminal.trim_matches('"')));
    }
    text
}
