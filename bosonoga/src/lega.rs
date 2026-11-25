use std::path::Path;

use crate::lexer::{Lexer, Tok, UNE_PREFIX};
use crate::source::SourceFile;
use crate::{Diagnostic, io_message, shown};

const FILE_NAME: &str = "lega.bosonoga";
const LANGUAGE_PATH: &str = "bosonoga_lega/lega.bosonoga";
const LANGUAGE: &str = include_str!("../../bosonoga_lega/lega.bosonoga");

// The words of the language that an application is allowed to use.
#[derive(Clone, Debug)]
pub struct Lega {
    words: Vec<String>,
    language: Vec<String>,
}

impl Lega {
    pub fn read(root: &Path) -> Result<Lega, String> {
        let path = root.join(FILE_NAME);
        let shown_path = path.display().to_string();
        match std::fs::read_to_string(&path) {
            Ok(text) => Lega::parse(&shown_path, &text),
            Err(error) => Err(format!(
                "{shown_path}: {}, and every bosonoga application has a {FILE_NAME} in its root",
                io_message(&error)
            )),
        }
    }

    pub fn parse(path: &str, text: &str) -> Result<Lega, String> {
        let language = match entries(LANGUAGE, None) {
            Ok(language) => language,
            Err(diagnostic) => return Err(diagnostic.located(LANGUAGE_PATH, LANGUAGE)),
        };
        match entries(text, Some(&language)) {
            Ok(words) => Ok(Lega { words, language }),
            Err(diagnostic) => Err(diagnostic.located(path, text)),
        }
    }

    pub(crate) fn check(&self, file: &SourceFile) -> Result<(), Diagnostic> {
        for spanned in Lexer::starting_at(&file.text, file.base, file.base) {
            let (start, token, end) = spanned?;
            let text = file.slice(start, end);
            for word in self.needed(&token, text) {
                if has(&self.words, word) {
                    continue;
                }
                if !has(&self.language, word) {
                    return Err(not_in_language(start, text, word));
                }
                return Err(Diagnostic::new(
                    start,
                    format!(
                        "'{}' is not lega, as {FILE_NAME} has no 'lega {word}'",
                        shown(text)
                    ),
                ));
            }
        }
        Ok(())
    }

    // The words that have to be lega for this token to be.
    fn needed<'a>(&'a self, token: &Tok<'_>, text: &'a str) -> Vec<&'a str> {
        if *token == Tok::End {
            return Vec::new();
        }
        if let Tok::Shift(_) = token {
            return vec!["shift_"];
        }
        if *token == Tok::Une {
            return vec![UNE_PREFIX];
        }
        if let Some(quote) = text.rfind('"') {
            return vec![&text[quote + 1..]];
        }
        if let Some(b'-' | b'0'..=b'9') = text.as_bytes().first()
            && let Some(suffix) = text.find('_')
        {
            return vec![&text[suffix..]];
        }
        if let Tok::Name(_) = token {
            return self.affixes(text);
        }
        vec![text]
    }

    // The prefix and the longest suffix of a name, as _optional_i32 and not
    // _i32 of count_optional_i32. A name without them is always lega.
    fn affixes<'a>(&'a self, name: &str) -> Vec<&'a str> {
        let mut prefix = "";
        let mut suffix = "";
        for word in &self.language {
            if word.len() >= name.len() {
                continue;
            }
            if word.ends_with('_') && name.starts_with(word) {
                prefix = word;
            }
            if word.starts_with('_') && name.ends_with(word) && word.len() > suffix.len() {
                suffix = word;
            }
        }
        let mut found = Vec::new();
        for affix in [prefix, suffix] {
            if !affix.is_empty() {
                found.push(affix);
            }
        }
        found
    }
}

fn has(words: &[String], word: &str) -> bool {
    for known in words {
        if known == word {
            return true;
        }
    }
    false
}

fn not_in_language(start: usize, text: &str, word: &str) -> Diagnostic {
    Diagnostic::new(
        start,
        format!(
            "'{}' cannot be lega, as bosonoga has no {}",
            shown(text),
            shown(word)
        ),
    )
}

// The words of a lega file, each after the word lega.
fn entries(text: &str, language: Option<&[String]>) -> Result<Vec<String>, Diagnostic> {
    let mut words = Vec::new();
    let mut after_lega = false;
    for spanned in Lexer::new(text) {
        let (start, token, end) = spanned?;
        let word = &text[start..end];
        if !after_lega {
            if token == Tok::End {
                break;
            }
            if word != "lega" {
                return Err(Diagnostic::new(
                    start,
                    format!("expected lega, as in lega di, got '{}'", shown(word)),
                ));
            }
            after_lega = true;
            continue;
        }
        if token == Tok::End {
            return Err(Diagnostic::new(start, "expected a word after lega"));
        }
        if let Some(known) = language
            && !has(known, word)
        {
            return Err(not_in_language(start, word, word));
        }
        if has(&words, word) {
            return Err(Diagnostic::new(
                start,
                format!("'{}' is already lega", shown(word)),
            ));
        }
        words.push(word.to_owned());
        after_lega = false;
    }
    Ok(words)
}
