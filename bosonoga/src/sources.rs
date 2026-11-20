use crate::ast::Span;
use crate::{Diagnostic, shown};

#[derive(Debug)]
pub(crate) struct SourceFile {
    pub(crate) path: String,
    pub(crate) text: String,
    pub(crate) base: usize,
}

#[derive(Debug, Default)]
pub(crate) struct Sources {
    pub(crate) files: Vec<SourceFile>,
}

impl Sources {
    pub(crate) fn add(&mut self, path: String, text: String) -> usize {
        let base = self
            .files
            .last()
            .map_or(0, |last| last.base + last.text.len() + 1);
        self.files.push(SourceFile { path, text, base });
        self.files.len() - 1
    }

    pub(crate) fn path(&self) -> &str {
        self.files.first().map_or("", |file| file.path.as_str())
    }

    pub(crate) fn file_at(&self, offset: usize) -> Option<&SourceFile> {
        self.files.iter().rev().find(|file| file.base <= offset)
    }

    pub(crate) fn text(&self, span: Span) -> &str {
        self.file_at(span.start)
            .and_then(|file| file.text.get(span.start - file.base..span.end - file.base))
            .map_or("", shown)
    }

    pub(crate) fn located(&self, diagnostic: &Diagnostic) -> String {
        match self.file_at(diagnostic.offset) {
            Some(file) => {
                Diagnostic::new(diagnostic.offset - file.base, diagnostic.message.clone())
                    .located(&file.path, &file.text)
            }
            None => diagnostic.message.clone(),
        }
    }
}
