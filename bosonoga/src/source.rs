use crate::ast::Span;
use crate::{Diagnostic, shown};

#[derive(Debug)]
pub(crate) struct SourceFile {
    pub(crate) path: String,
    pub(crate) text: String,
    pub(crate) base: usize,
}

impl SourceFile {
    pub(crate) fn slice(&self, start: usize, end: usize) -> &str {
        self.text
            .get(start - self.base..end - self.base)
            .unwrap_or("")
    }
}

#[derive(Debug, Default)]
pub(crate) struct Sources {
    pub(crate) files: Vec<SourceFile>,
}

impl Sources {
    pub(crate) fn add(&mut self, path: String, text: String) -> usize {
        let mut base = 0;
        if let Some(last) = self.files.last() {
            base = last.base + last.text.len() + 1;
        }
        self.files.push(SourceFile { path, text, base });
        self.files.len() - 1
    }

    pub(crate) fn path(&self) -> &str {
        match self.files.first() {
            Some(file) => &file.path,
            None => "",
        }
    }

    pub(crate) fn file_at(&self, offset: usize) -> Option<&SourceFile> {
        let mut found = None;
        for file in &self.files {
            if file.base <= offset {
                found = Some(file);
            }
        }
        found
    }

    pub(crate) fn text(&self, span: Span) -> &str {
        match self.file_at(span.start) {
            Some(file) => shown(file.slice(span.start, span.end)),
            None => "",
        }
    }

    pub(crate) fn located(&self, diagnostic: &Diagnostic) -> String {
        match self.file_at(diagnostic.offset) {
            Some(file) => {
                let local =
                    Diagnostic::new(diagnostic.offset - file.base, diagnostic.message.clone());
                local.located(&file.path, &file.text)
            }
            None => diagnostic.message.clone(),
        }
    }
}
