use crate::Diagnostic;
use crate::value::{Compare, Kind, Value};

#[derive(Clone, Debug, PartialEq)]
pub enum Tok<'input> {
    BosonogaMain,
    Di,
    Do,
    Info,
    Nihil,
    Return,
    ExitSuccess,
    ExitFailure,
    Var,
    Val,
    Is,
    Set,
    Operation(crate::value::Operation),
    If,
    Else,
    And,
    Or,
    Not,
    ShiftOr,
    Compare(Compare),
    Literal(Value),
    Shift(&'input str),
    Name(&'input str),
    Result(&'input str),
    GfxValue(&'input str),
    Reserved(&'input str),
    IndexedType(&'input str),
    End,
}

pub type Spanned<'input> = Result<(usize, Tok<'input>, usize), Diagnostic>;

const RESERVED: &[&str] = &[
    "set",
    "unset",
    "new",
    "with",
    "to_add",
    "to_subtract",
    "to_multiply",
    "to_divide",
    "const",
    "io_raw_import",
    "mod",
    "pub",
    "to_f64",
    "math_to_log",
    "math_to_log10",
    "math_to_log1p",
    "math_to_expm1",
    "math_to_hypot",
    "math_to_exp",
    "math_to_asin",
    "math_to_acos",
    "math_to_tan",
    "to_f32",
    "to_i32",
    "to_i64",
    "to_u32",
    "to_u64",
    "math_to_sin",
    "math_to_cos",
    "math_to_atan2",
    "math_to_sqrt",
    "math_to_pow",
    "math_to_abs",
    "math_to_min",
    "math_to_max",
    "math_to_floor",
    "math_to_ceil",
    "math_to_round",
    "sleep_ms",
    "gfx_await_next_frame",
    "gfx_init",
    "gfx_frame",
    "gfx_main",
    "gfx_cleanup",
    "gfx_clear",
    "gfx_draw_rectangle",
    "gfx_draw_hex",
    "gfx_draw_circle",
    "gfx_draw_text",
    "gfx_draw_icon",
    "gfx_key_down",
    "bosonoga_global",
    "while",
    "break",
    "continue",
    "to_mod",
    "to_join",
    "io_read",
    "io_write",
    "io_modified",
    "xml_child",
    "xml_attribute",
    "str_parse",
    "def",
    "ret",
    "call",
    "to",
    "array",
    "row",
    "with_len_col",
    "len_col",
    "len_row",
    "expect",
    "at",
    "of",
    "as",
    "ret_void",
];

const INDEXED_TYPES: &[&str] = &[
    "flags",
    "color",
    "str",
    "bool",
    "input_key",
    "font",
    "icon",
    "xml_str",
    "dir",
];

pub struct Lexer<'input> {
    source: &'input str,
    cursor: usize,
    ended: bool,
}

impl<'input> Lexer<'input> {
    #[must_use]
    pub fn new(source: &'input str) -> Self {
        Self::starting_at(source, 0)
    }

    #[must_use]
    pub fn starting_at(source: &'input str, cursor: usize) -> Self {
        Lexer {
            source,
            cursor,
            ended: false,
        }
    }

    fn byte(&self, at: usize) -> Option<u8> {
        self.source.as_bytes().get(at).copied()
    }

    fn at_word_char(&self) -> bool {
        self.byte(self.cursor)
            .is_some_and(|b| b.is_ascii_alphanumeric() || b == b'_')
    }

    fn at_digit(&self) -> bool {
        self.byte(self.cursor).is_some_and(|b| b.is_ascii_digit())
    }

    fn consume_digits(&mut self) {
        while self.at_digit() {
            self.cursor += 1;
        }
    }

    fn consume_word(&mut self) -> &'input str {
        let start = self.cursor;
        while self.at_word_char() {
            self.cursor += 1;
        }
        &self.source[start..self.cursor]
    }

    fn skip_blanks_and_comments(&mut self) {
        while let Some(b) = self.byte(self.cursor) {
            if is_space(b) {
                self.cursor += 1;
            } else if b == b'/' && self.byte(self.cursor + 1) == Some(b'/') {
                while self.byte(self.cursor).is_some_and(|c| c != b'\n') {
                    self.cursor += 1;
                }
            } else {
                return;
            }
        }
    }

    fn at_number(&self) -> bool {
        let digit = if self.byte(self.cursor) == Some(b'-') {
            self.cursor + 1
        } else {
            self.cursor
        };
        self.byte(digit).is_some_and(|b| b.is_ascii_digit())
    }

    fn lex_number(&mut self, start: usize) -> Result<Tok<'input>, Diagnostic> {
        if self.byte(self.cursor) == Some(b'-') {
            self.cursor += 1;
        }
        self.consume_digits();
        if self.byte(self.cursor) == Some(b'.') {
            return Err(Diagnostic::new(
                start,
                "the decimal separator is the letter d, as in 1d5_f32",
            ));
        }
        let mut has_fraction = false;
        if self.byte(self.cursor) == Some(b'd') {
            has_fraction = true;
            self.cursor += 1;
            if !self.at_digit() {
                return Err(Diagnostic::new(
                    start,
                    "expected a digit after the decimal separator d",
                ));
            }
            self.consume_digits();
        }
        let number = &self.source[start..self.cursor];
        let suffix = self.consume_word();
        let Some(name) = suffix.strip_prefix('_') else {
            return Err(suffix_error(start));
        };
        if INDEXED_TYPES.contains(&name) {
            if has_fraction || number.starts_with('-') {
                return Err(Diagnostic::new(
                    start,
                    "an index is digits and a type, like 1_str",
                ));
            }
            return Ok(Tok::IndexedType(&self.source[start..self.cursor]));
        }
        let kind = match name {
            "i32" => Kind::I32,
            "i64" => Kind::I64,
            "u32" => Kind::U32,
            "u64" => Kind::U64,
            "f32" => Kind::F32,
            "f64" => Kind::F64,
            _ => return Err(suffix_error(start)),
        };
        if kind.is_integer() && has_fraction {
            return Err(Diagnostic::new(start, "an integer cannot have a fraction"));
        }
        let value = if kind.is_integer() {
            number
                .parse()
                .ok()
                .and_then(|whole| Value::whole(kind, whole))
        } else {
            number
                .replace('d', ".")
                .parse()
                .ok()
                .and_then(|real| Value::real(kind, real))
        };
        value.map(Tok::Literal).ok_or_else(|| {
            Diagnostic::new(
                start,
                format!("the number is outside the {} range", kind.name()),
            )
        })
    }

    fn lex_string(&mut self, start: usize) -> Result<Tok<'input>, Diagnostic> {
        self.cursor += 1;
        let text_start = self.cursor;
        loop {
            match self.byte(self.cursor) {
                None | Some(b'\n') => return Err(Diagnostic::new(start, "unterminated string")),
                Some(b'\\') => {
                    return Err(Diagnostic::new(
                        self.cursor,
                        "escape sequences are not supported",
                    ));
                }
                Some(0) => return Err(Diagnostic::new(self.cursor, "NUL byte in string")),
                Some(b'"') => break,
                Some(_) => self.cursor += 1,
            }
        }
        let text = &self.source[text_start..self.cursor];
        self.cursor += 1;
        if self.consume_word() != "_str" {
            return Err(Diagnostic::new(
                start,
                "a string must have the suffix _str, as in \"text\"_str",
            ));
        }
        Ok(Tok::Literal(Value::Str(text.to_owned())))
    }
}

fn suffix_error(start: usize) -> Diagnostic {
    Diagnostic::new(
        start,
        "number must have the suffix _i32, _i64, _u32, _u64, _f32 or _f64",
    )
}

// g_ascii_isspace, which counts vertical tab and form feed too.
fn is_space(b: u8) -> bool {
    matches!(b, b' ' | b'\t' | b'\n' | b'\r' | 0x0b | 0x0c)
}

fn is_numbered(text: &str, prefix: &str) -> bool {
    text.strip_prefix(prefix)
        .is_some_and(|digits| !digits.is_empty() && digits.bytes().all(|b| b.is_ascii_digit()))
}

fn word(text: &str) -> Tok<'_> {
    if let Some(compare) = Compare::named(text) {
        return Tok::Compare(compare);
    }
    match text {
        "bosonoga_main" => Tok::BosonogaMain,
        "di" => Tok::Di,
        "do" => Tok::Do,
        "info" => Tok::Info,
        "nihil" => Tok::Nihil,
        "return" => Tok::Return,
        "exit_success" => Tok::ExitSuccess,
        "exit_failure" => Tok::ExitFailure,
        "set" => Tok::Set,
        "to" => Tok::Operation(crate::value::Operation::Copy),
        "to_add" => Tok::Operation(crate::value::Operation::Add),
        "to_subtract" => Tok::Operation(crate::value::Operation::Subtract),
        "to_multiply" => Tok::Operation(crate::value::Operation::Multiply),
        "to_divide" => Tok::Operation(crate::value::Operation::Divide),
        "to_mod" => Tok::Operation(crate::value::Operation::Modulo),
        "math_to_pow" => Tok::Operation(crate::value::Operation::Pow),
        "math_to_sqrt" => Tok::Operation(crate::value::Operation::Sqrt),
        "var" => Tok::Var,
        "val" => Tok::Val,
        "is" => Tok::Is,
        "if" => Tok::If,
        "else" => Tok::Else,
        "and" => Tok::And,
        "or" => Tok::Or,
        "not" => Tok::Not,
        "shift_or" => Tok::ShiftOr,
        "true_bool" => Tok::Literal(Value::Bool(true)),
        "false_bool" => Tok::Literal(Value::Bool(false)),
        "gfx_delta_time" | "gfx_key" => Tok::GfxValue(text),
        _ if is_numbered(text, "shift_") => Tok::Shift(text),
        _ if text.starts_with("ret_") && text.as_bytes().get(4).is_some_and(u8::is_ascii_digit) => {
            Tok::Result(text)
        }
        _ if is_numbered(text, "to_") || RESERVED.contains(&text) => Tok::Reserved(text),
        _ => Tok::Name(text),
    }
}

impl<'input> Iterator for Lexer<'input> {
    type Item = Spanned<'input>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.ended {
            return None;
        }
        self.skip_blanks_and_comments();
        let start = self.cursor;
        let token = match self.byte(start) {
            None => {
                self.ended = true;
                Ok(Tok::End)
            }
            Some(b'"') => self.lex_string(start),
            Some(_) if self.at_number() => self.lex_number(start),
            Some(b) if b.is_ascii_alphabetic() || b == b'_' => Ok(word(self.consume_word())),
            Some(_) => Err(Diagnostic::new(start, "unexpected character")),
        };
        if token.is_err() {
            self.ended = true;
        }
        Some(token.map(|t| (start, t, self.cursor)))
    }
}
