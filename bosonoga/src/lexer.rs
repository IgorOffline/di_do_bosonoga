use crate::Diagnostic;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Tok<'input> {
    BosonogaMain,
    Di,
    Do,
    Info,
    Nihil,
    Return,
    ExitSuccess,
    ExitFailure,
    I32(i32),
    Word(&'input str),
    End,
}

pub type Spanned<'input> = Result<(usize, Tok<'input>, usize), Diagnostic>;

pub struct Lexer<'input> {
    source: &'input str,
    cursor: usize,
    ended: bool,
}

impl<'input> Lexer<'input> {
    #[must_use]
    pub fn new(source: &'input str) -> Self {
        Lexer {
            source,
            cursor: 0,
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

    fn consume_digits(&mut self) {
        while self.byte(self.cursor).is_some_and(|b| b.is_ascii_digit()) {
            self.cursor += 1;
        }
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
        let number_end = self.cursor;
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
            if !self.byte(self.cursor).is_some_and(|b| b.is_ascii_digit()) {
                return Err(Diagnostic::new(
                    start,
                    "expected a digit after the decimal separator d",
                ));
            }
            self.consume_digits();
        }
        let suffix_start = self.cursor;
        while self.at_word_char() {
            self.cursor += 1;
        }
        if &self.source[suffix_start..self.cursor] != "_i32" {
            return Err(Diagnostic::new(start, "number must have the suffix _i32"));
        }
        if has_fraction {
            return Err(Diagnostic::new(start, "an integer cannot have a fraction"));
        }
        self.source[start..number_end]
            .parse()
            .map(Tok::I32)
            .map_err(|_| Diagnostic::new(start, "the number is outside the i32 range"))
    }
}

// g_ascii_isspace, which counts vertical tab and form feed too.
fn is_space(b: u8) -> bool {
    matches!(b, b' ' | b'\t' | b'\n' | b'\r' | 0x0b | 0x0c)
}

fn word(text: &str) -> Tok<'_> {
    match text {
        "bosonoga_main" => Tok::BosonogaMain,
        "di" => Tok::Di,
        "do" => Tok::Do,
        "info" => Tok::Info,
        "nihil" => Tok::Nihil,
        "return" => Tok::Return,
        "exit_success" => Tok::ExitSuccess,
        "exit_failure" => Tok::ExitFailure,
        _ => Tok::Word(text),
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
            Some(_) if self.at_number() => self.lex_number(start),
            Some(b) if b.is_ascii_alphabetic() || b == b'_' => {
                while self.at_word_char() {
                    self.cursor += 1;
                }
                Ok(word(&self.source[start..self.cursor]))
            }
            Some(_) => Err(Diagnostic::new(start, "unexpected character")),
        };
        if token.is_err() {
            self.ended = true;
        }
        Some(token.map(|t| (start, t, self.cursor)))
    }
}
