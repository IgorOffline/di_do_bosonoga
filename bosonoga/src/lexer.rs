use crate::Diagnostic;
use crate::ast::Section;
use crate::value::{Compare, Conversion, Kind, Operation, Value};

#[derive(Clone, Debug, PartialEq)]
pub enum Tok<'input> {
    Section(Section),
    Di,
    Do,
    Info,
    Call,
    Given(crate::io::Builtin),
    Join,
    As,
    Nihil,
    Return,
    ExitSuccess,
    ExitFailure,
    Var,
    Val,
    Is,
    Set,
    Operation(Operation),
    Conversion(Conversion),
    Const,
    Pub,
    Mod,
    Def,
    FromGlobal,
    Ret,
    RetVoid,
    Of,
    With,
    At,
    Array,
    Row,
    While,
    AwaitNextFrame,
    GfxClear,
    GfxDrawRectangle,
    GfxDrawHex,
    GfxDrawCircle,
    GfxDrawText,
    GfxDrawIcon,
    WithLenCol,
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
    "unset",
    "new",
    "io_raw_import",
    "sleep_ms",
    "break",
    "continue",
    "len_col",
    "len_row",
    "expect",
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
    base: usize,
    cursor: usize,
    ended: bool,
}

impl<'input> Lexer<'input> {
    #[must_use]
    pub fn new(source: &'input str) -> Self {
        Self::starting_at(source, 0, 0)
    }

    #[must_use]
    pub fn starting_at(source: &'input str, base: usize, offset: usize) -> Self {
        Lexer {
            source,
            base,
            cursor: offset - base,
            ended: false,
        }
    }

    fn byte(&self, at: usize) -> Option<u8> {
        self.source.as_bytes().get(at).copied()
    }

    fn is_digit_at(&self, at: usize) -> bool {
        match self.byte(at) {
            Some(b) => b.is_ascii_digit(),
            None => false,
        }
    }

    fn at_word_char(&self) -> bool {
        match self.byte(self.cursor) {
            Some(b) => b.is_ascii_alphanumeric() || b == b'_',
            None => false,
        }
    }

    fn at_digit(&self) -> bool {
        self.is_digit_at(self.cursor)
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
                self.skip_line();
            } else {
                return;
            }
        }
    }

    fn skip_line(&mut self) {
        while let Some(b) = self.byte(self.cursor) {
            if b == b'\n' {
                return;
            }
            self.cursor += 1;
        }
    }

    fn at_number(&self) -> bool {
        if self.byte(self.cursor) == Some(b'-') {
            return self.is_digit_at(self.cursor + 1);
        }
        self.is_digit_at(self.cursor)
    }

    fn lex_number(&mut self, start: usize) -> Result<Tok<'input>, Diagnostic> {
        if self.byte(self.cursor) == Some(b'-') {
            self.cursor += 1;
        }
        self.consume_digits();
        if self.byte(self.cursor) == Some(b'.') {
            return Err(Diagnostic::new(
                self.base + start,
                "the decimal separator is the letter d, as in 1d5_f32",
            ));
        }
        let mut has_fraction = false;
        if self.byte(self.cursor) == Some(b'd') {
            has_fraction = true;
            self.cursor += 1;
            if !self.at_digit() {
                return Err(Diagnostic::new(
                    self.base + start,
                    "expected a digit after the decimal separator d",
                ));
            }
            self.consume_digits();
        }
        let number = &self.source[start..self.cursor];
        let suffix = self.consume_word();
        let Some(name) = suffix.strip_prefix('_') else {
            return Err(suffix_error(self.base + start));
        };
        if INDEXED_TYPES.contains(&name) {
            if has_fraction || number.starts_with('-') {
                return Err(Diagnostic::new(
                    self.base + start,
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
            _ => return Err(suffix_error(self.base + start)),
        };
        if kind.is_integer() && has_fraction {
            return Err(Diagnostic::new(
                self.base + start,
                "an integer cannot have a fraction",
            ));
        }
        let mut value = None;
        if kind.is_integer() {
            if let Ok(whole) = number.parse::<i128>() {
                value = Value::whole(kind, whole);
            }
        } else if let Ok(real) = number.replace('d', ".").parse::<f64>() {
            value = Value::real(kind, real);
        }
        match value {
            Some(literal) => Ok(Tok::Literal(literal)),
            None => Err(Diagnostic::new(
                self.base + start,
                format!("the number is outside the {} range", kind.name()),
            )),
        }
    }

    fn lex_string(&mut self, start: usize) -> Result<Tok<'input>, Diagnostic> {
        self.cursor += 1;
        let text_start = self.cursor;
        loop {
            match self.byte(self.cursor) {
                None | Some(b'\n') => {
                    return Err(Diagnostic::new(self.base + start, "unterminated string"));
                }
                Some(b'\\') => {
                    return Err(Diagnostic::new(
                        self.base + self.cursor,
                        "escape sequences are not supported",
                    ));
                }
                Some(0) => {
                    return Err(Diagnostic::new(
                        self.base + self.cursor,
                        "NUL byte in string",
                    ));
                }
                Some(b'"') => break,
                Some(_) => self.cursor += 1,
            }
        }
        let text = &self.source[text_start..self.cursor];
        self.cursor += 1;
        if self.consume_word() != "_str" {
            return Err(Diagnostic::new(
                self.base + start,
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
    let Some(digits) = text.strip_prefix(prefix) else {
        return false;
    };
    if digits.is_empty() {
        return false;
    }
    for b in digits.bytes() {
        if !b.is_ascii_digit() {
            return false;
        }
    }
    true
}

fn is_result_name(text: &str) -> bool {
    if !text.starts_with("ret_") {
        return false;
    }
    match text.as_bytes().get(4) {
        Some(b) => b.is_ascii_digit(),
        None => false,
    }
}

fn word(text: &str) -> Tok<'_> {
    if let Some(compare) = Compare::named(text) {
        return Tok::Compare(compare);
    }
    if let Some(section) = Section::named(text) {
        return Tok::Section(section);
    }
    if let Some(operation) = Operation::named(text) {
        return Tok::Operation(operation);
    }
    if let Some(conversion) = Conversion::named(text) {
        return Tok::Conversion(conversion);
    }
    if let Some(builtin) = crate::io::Builtin::named(text) {
        return Tok::Given(builtin);
    }
    match text {
        "to_join" => Tok::Join,
        "as" => Tok::As,
        "di" => Tok::Di,
        "do" => Tok::Do,
        "info" => Tok::Info,
        "call" => Tok::Call,
        "nihil" => Tok::Nihil,
        "return" => Tok::Return,
        "exit_success" => Tok::ExitSuccess,
        "exit_failure" => Tok::ExitFailure,
        "set" => Tok::Set,
        "const" => Tok::Const,
        "pub" => Tok::Pub,
        "mod" => Tok::Mod,
        "def" => Tok::Def,
        "from_global" => Tok::FromGlobal,
        "ret" => Tok::Ret,
        "ret_void" => Tok::RetVoid,
        "of" => Tok::Of,
        "with" => Tok::With,
        "at" => Tok::At,
        "array" => Tok::Array,
        "row" => Tok::Row,
        "while" => Tok::While,
        "gfx_await_next_frame" => Tok::AwaitNextFrame,
        "gfx_clear" => Tok::GfxClear,
        "gfx_draw_rectangle" => Tok::GfxDrawRectangle,
        "gfx_draw_hex" => Tok::GfxDrawHex,
        "gfx_draw_circle" => Tok::GfxDrawCircle,
        "gfx_draw_text" => Tok::GfxDrawText,
        "gfx_draw_icon" => Tok::GfxDrawIcon,
        "with_len_col" => Tok::WithLenCol,
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
        _ if is_result_name(text) => Tok::Result(text),
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
            Some(_) => Err(Diagnostic::new(self.base + start, "unexpected character")),
        };
        match token {
            Ok(lexed) => Some(Ok((self.base + start, lexed, self.base + self.cursor))),
            Err(diagnostic) => {
                self.ended = true;
                Some(Err(diagnostic))
            }
        }
    }
}
