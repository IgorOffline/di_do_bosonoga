use std::mem;

use crate::ast::{
    self, Arguments, ExitStatus, Initial, Item, Named, Results, Rows, Section, Span, Term,
};
use crate::gfx::{Align, Horizontal, Vertical};
use crate::lexer::{Lexer, Tok};
use crate::program::{
    self, Array, Callee, Comparison, Condition, Draw, Operand, Program, Statement, VarArray,
};
use crate::settings::Settings;
use crate::sources::Sources;
use crate::value::{Compare, Conversion, Kind, Operation, Value};
use crate::{Diagnostic, shown};

const SHIFT_BIT_MAX: u64 = 31;
const FUNCTION_VALUES_MAX: usize = 16;
const INDEX_DIGITS_MAX: usize = 9;
const ARRAY_VALUES_MAX: usize = 65_536;
const SETTING_PREFIXES: [&str; 6] = ["color_", "key_", "font_", "icon_", "dir_", "global_"];

#[derive(Clone, Copy)]
struct Declaration {
    span: Span,
    scope: usize,
    public: bool,
    used: bool,
}

struct Variable<'input> {
    name: &'input str,
    kind: Kind,
    slot: usize,
    mutable: bool,
    array: Option<(usize, Vec<Kind>)>,
    declaration: Declaration,
}

struct Constant<'input> {
    name: &'input str,
    value: Value,
    declaration: Declaration,
}

#[derive(Clone, Copy)]
enum Callable {
    Function(usize),
    Array(usize),
    VarArray(usize),
}

struct Function<'input> {
    name: &'input str,
    callable: Callable,
    parameters: Vec<Kind>,
    results: Vec<Kind>,
    declaration: Declaration,
}

impl Function<'_> {
    fn noun(&self) -> &'static str {
        match self.callable {
            Callable::Function(_) => "function",
            Callable::Array(_) | Callable::VarArray(_) => "array",
        }
    }

    fn gives(&self) -> &'static str {
        match self.callable {
            Callable::Function(_) => "returns",
            Callable::Array(_) | Callable::VarArray(_) => "has rows of",
        }
    }
}

struct Scope {
    file: usize,
    parent: usize,
}

struct Checker<'a, 'input> {
    sources: &'a Sources,
    settings: &'a Settings,
    files: &'a [ast::File<'input>],
    modules: &'a [Vec<usize>],
    scopes: Vec<Scope>,
    scope: usize,
    variables: Vec<Variable<'input>>,
    constants: Vec<Constant<'input>>,
    functions: Vec<Function<'input>>,
    slot_count: usize,
    section: Option<Section>,
    function: Option<usize>,
    depth: usize,
    seen: Vec<Section>,
    program: Program,
}

pub(crate) fn check(
    sources: &Sources,
    settings: &Settings,
    files: &[ast::File<'_>],
    modules: &[Vec<usize>],
) -> Result<Program, Diagnostic> {
    let mut checker = Checker {
        sources,
        settings,
        files,
        modules,
        scopes: vec![Scope { file: 0, parent: 0 }],
        scope: 0,
        variables: Vec::new(),
        constants: Vec::new(),
        functions: Vec::new(),
        slot_count: 0,
        section: None,
        function: None,
        depth: 0,
        seen: Vec::new(),
        program: Program {
            background: settings.background,
            ..Program::default()
        },
    };
    checker.items(0)?;
    if checker.program.sections.main.is_none() {
        let end = files.first().map_or(0, |file| file.end);
        return Err(Diagnostic::new(
            end,
            if checker.program.is_gfx {
                "a gfx program needs a gfx_main block"
            } else {
                "a program needs a bosonoga_main block"
            },
        ));
    }
    checker.check_all_used()?;
    checker.program.slot_count = checker.slot_count;
    Ok(checker.program)
}

fn is_function_name(name: &str) -> bool {
    name.len() > "_fn".len() && name.ends_with("_fn")
}

fn split_indexed(text: &str) -> Option<(usize, Kind)> {
    let digits = text.bytes().take_while(u8::is_ascii_digit).count();
    let (number, rest) = text.split_at(digits);
    if digits == 0 || digits > INDEX_DIGITS_MAX || (digits > 1 && number.starts_with('0')) {
        return None;
    }
    let kind = Kind::named(rest.strip_prefix('_')?)?;
    Some((number.parse().ok()?, kind))
}

fn parameter_kind(name: &str) -> Option<Kind> {
    (2..name.len())
        .filter(|start| name.as_bytes().get(start - 1) == Some(&b'_'))
        .find_map(|start| Kind::named(name.get(start..)?))
}

fn values_shown(kinds: &[Kind]) -> String {
    if kinds.is_empty() {
        return "no values".to_owned();
    }
    let names: Vec<&str> = kinds.iter().map(|kind| kind.name()).collect();
    format!(
        "{} {}, {}",
        kinds.len(),
        if kinds.len() == 1 { "value" } else { "values" },
        names.join(" ")
    )
}

fn kinds_named(kinds: &[Kind]) -> String {
    let named: Vec<String> = kinds
        .iter()
        .enumerate()
        .map(|(index, kind)| format!("{index}_{}", kind.name()))
        .collect();
    named.join(", ")
}

impl<'a, 'input> Checker<'a, 'input> {
    fn text(&self, span: Span) -> &'a str {
        self.sources.text(span)
    }

    fn fail<T>(offset: usize, message: impl Into<String>) -> Result<T, Diagnostic> {
        Err(Diagnostic::new(offset, message))
    }

    fn next_token(&self, after: usize) -> (usize, String) {
        let Some(file) = self.sources.file_at(after) else {
            return (after, "end of file".to_owned());
        };
        match Lexer::starting_at(&file.text, file.base, after).next() {
            Some(Ok((start, token, end))) if token != Tok::End => (
                start,
                format!(
                    "'{}'",
                    shown(
                        file.text
                            .get(start - file.base..end - file.base)
                            .unwrap_or("")
                    )
                ),
            ),
            Some(Ok((start, ..)) | Err(Diagnostic { offset: start, .. })) => {
                (start, "end of file".to_owned())
            }
            None => (after, "end of file".to_owned()),
        }
    }

    fn expected<T>(&self, after: usize, what: &str) -> Result<T, Diagnostic> {
        let (at, got) = self.next_token(after);
        Self::fail(at, format!("expected {what}, got {got}"))
    }

    fn expected_at<T>(&self, span: Span, what: &str) -> Result<T, Diagnostic> {
        Self::fail(
            span.start,
            format!("expected {what}, got '{}'", self.text(span)),
        )
    }

    fn declaration(&self, span: Span, public: bool) -> Declaration {
        Declaration {
            span,
            scope: self.scope,
            public,
            used: false,
        }
    }

    fn visible(&self, declaration: &Declaration) -> bool {
        let home = if declaration.public {
            self.scopes
                .get(declaration.scope)
                .map_or(0, |scope| scope.parent)
        } else {
            declaration.scope
        };
        let mut scope = self.scope;
        while scope != home {
            if scope == 0 {
                return false;
            }
            scope = self.scopes.get(scope).map_or(0, |outer| outer.parent);
        }
        true
    }

    fn constant_any(&self, name: &str) -> Option<usize> {
        self.constants.iter().position(|c| c.name == name)
    }

    fn constant(&self, name: &str) -> Option<usize> {
        self.constant_any(name)
            .filter(|&i| self.visible(&self.constants[i].declaration))
    }

    fn variable_any(&self, name: &str) -> Option<usize> {
        self.variables.iter().position(|v| v.name == name)
    }

    fn variable(&self, name: &str) -> Option<usize> {
        self.variable_any(name)
            .filter(|&i| self.visible(&self.variables[i].declaration))
    }

    fn function_any(&self, name: &str) -> Option<usize> {
        self.functions.iter().position(|f| f.name == name)
    }

    fn function(&self, name: &str) -> Option<usize> {
        self.function_any(name)
            .filter(|&i| self.visible(&self.functions[i].declaration))
    }

    fn is_reserved(&self, name: &str) -> bool {
        SETTING_PREFIXES.iter().any(|prefix| {
            name.strip_prefix(prefix).is_some_and(|key| match *prefix {
                "color_" => self.settings.color(key).is_some(),
                "key_" => self.settings.key(key).is_some(),
                "font_" => self.settings.font(key).is_some(),
                "icon_" => self.settings.icon(key).is_some(),
                "dir_" => self.settings.dir(key).is_some(),
                _ => self.settings.global(key).is_some(),
            })
        })
    }

    fn setting(&mut self, name: &str) -> Option<Value> {
        if let Some(key) = name.strip_prefix("color_") {
            return self.settings.color(key).map(Value::Color);
        }
        if let Some(key) = name.strip_prefix("key_") {
            return self.settings.key(key).map(Value::InputKey);
        }
        if let Some(key) = name.strip_prefix("font_") {
            let asset = self.settings.font(key)?;
            let fonts = &mut self.program.fonts;
            let index = fonts
                .iter()
                .position(|(known, _)| known == key)
                .unwrap_or_else(|| {
                    fonts.push((key.to_owned(), asset.clone()));
                    fonts.len() - 1
                });
            return Some(Value::Font {
                index,
                text: asset.text.clone(),
            });
        }
        if let Some(key) = name.strip_prefix("icon_") {
            let asset = self.settings.icon(key)?;
            let icons = &mut self.program.icons;
            let index = icons
                .iter()
                .position(|(known, _)| known == key)
                .unwrap_or_else(|| {
                    icons.push((key.to_owned(), asset.clone()));
                    icons.len() - 1
                });
            return Some(Value::Icon {
                index,
                text: asset.text.clone(),
            });
        }
        name.strip_prefix("global_")
            .and_then(|key| self.settings.global(key))
            .cloned()
    }

    fn hidden(&self, name: &str) -> Option<&Declaration> {
        if let Some(i) = self.constant_any(name) {
            return Some(&self.constants[i].declaration);
        }
        if let Some(i) = self.variable_any(name) {
            return Some(&self.variables[i].declaration);
        }
        self.function_any(name)
            .map(|i| &self.functions[i].declaration)
    }

    fn check_not_hidden(&self, span: Span, name: &str) -> Result<(), Diagnostic> {
        let Some(declaration) = self.hidden(name).filter(|d| !self.visible(d)) else {
            return Ok(());
        };
        let path_of = |scope: usize| {
            self.scopes
                .get(scope)
                .and_then(|s| self.sources.files.get(s.file))
                .map_or("", |file| file.path.as_str())
        };
        let shown = self.text(span);
        if !declaration.public {
            return Self::fail(
                span.start,
                format!("'{shown}' is private to {}", path_of(declaration.scope)),
            );
        }
        let parent = self
            .scopes
            .get(declaration.scope)
            .map_or(0, |scope| scope.parent);
        Self::fail(
            span.start,
            format!(
                "'{shown}' is pub in {}, so only {}, which reads it with mod, sees it",
                path_of(declaration.scope),
                path_of(parent)
            ),
        )
    }

    fn fail_unknown_variable<T>(&self, span: Span, name: &str) -> Result<T, Diagnostic> {
        self.check_not_hidden(span, name)?;
        let shown = self.text(span);
        let is_var_array = self
            .variable(name)
            .is_some_and(|i| self.variables[i].array.is_some());
        if is_var_array {
            return Self::fail(
                span.start,
                format!("'{shown}' is an array, which is read with var, set ... to or call"),
            );
        }
        if let Some(i) = self.function(name) {
            let (what, done) = match self.functions[i].callable {
                Callable::Function(_) => ("a function", "called"),
                Callable::Array(_) | Callable::VarArray(_) => ("an array", "read"),
            };
            return Self::fail(
                span.start,
                format!("'{shown}' is {what}, which is {done} with var, set ... to or call"),
            );
        }
        if is_function_name(name) {
            return Self::fail(span.start, format!("unknown function '{shown}'"));
        }
        Self::fail(span.start, format!("unknown variable '{shown}'"))
    }

    fn new_name(&self, (span, name): Named<'input>) -> Result<(), Diagnostic> {
        if self.is_reserved(name) {
            return self.expected_at(span, "a variable name");
        }
        let shown = self.text(span);
        if self.constant_any(name).is_some() {
            return Self::fail(span.start, "a constant with this name already exists");
        }
        if self.variable_any(name).is_some() {
            return Self::fail(span.start, format!("variable '{shown}' already exists"));
        }
        if is_function_name(name) {
            return Self::fail(
                span.start,
                format!("'{shown}' ends with _fn, which only the name of a function does"),
            );
        }
        if let Some(i) = self.function_any(name) {
            let what = match self.functions[i].callable {
                Callable::Function(_) => "a function",
                Callable::Array(_) | Callable::VarArray(_) => "an array",
            };
            return Self::fail(span.start, format!("'{shown}' is already {what}"));
        }
        Ok(())
    }

    fn allocate(&mut self) -> usize {
        self.slot_count += 1;
        self.slot_count - 1
    }

    fn declare(&mut self, (span, name): Named<'input>, kind: Kind, slot: usize, mutable: bool) {
        let declaration = self.declaration(span, false);
        self.variables.push(Variable {
            name,
            kind,
            slot,
            mutable,
            array: None,
            declaration,
        });
    }

    fn public(&self, public: Option<Span>) -> Result<bool, Diagnostic> {
        match public {
            Some(span) if self.scope == 0 => {
                Self::fail(span.start, "pub is only in a module, a file read with mod")
            }
            Some(_) => Ok(true),
            None => Ok(false),
        }
    }

    fn items(&mut self, file: usize) -> Result<(), Diagnostic> {
        let files = self.files;
        let mut modules = self.modules.get(file).into_iter().flatten();
        let Some(source) = files.get(file) else {
            return Ok(());
        };
        for item in &source.items {
            match item {
                Item::Mod(..) => {
                    let Some(&module) = modules.next() else {
                        continue;
                    };
                    let scope = self.scopes.len();
                    self.scopes.push(Scope {
                        file: module,
                        parent: self.scope,
                    });
                    let outer = mem::replace(&mut self.scope, scope);
                    self.items(module)?;
                    self.scope = outer;
                }
                Item::Const {
                    public,
                    name,
                    value,
                } => {
                    let is_public = self.public(*public)?;
                    self.new_name(*name)?;
                    let Operand::Literal(literal) = self.value(value)?.0 else {
                        return Self::fail(
                            value.span.start,
                            "a constant needs a literal or an earlier constant",
                        );
                    };
                    let declaration = self.declaration(name.0, is_public);
                    self.constants.push(Constant {
                        name: name.1,
                        value: literal,
                        declaration,
                    });
                }
                Item::Def {
                    public,
                    name,
                    parameters,
                    results,
                    block,
                } => self.def(*public, *name, parameters, results, block)?,
                Item::Array {
                    public,
                    name,
                    kinds,
                    rows,
                } => self.array(*public, *name, kinds, rows)?,
                Item::Section(span, section, block, after) => {
                    self.section(*span, *section, block, *after)?;
                }
            }
        }
        Ok(())
    }

    fn section(
        &mut self,
        span: Span,
        section: Section,
        block: &[ast::Statement<'input>],
        after: usize,
    ) -> Result<(), Diagnostic> {
        let has = |kind: Section| self.seen.contains(&kind);
        let has_gfx = self.seen.iter().any(|kind| kind.is_gfx());
        if section == Section::Global && (has(Section::Main) || has_gfx || has(Section::Global)) {
            return Self::fail(span.start, "bosonoga_global must be the first section");
        }
        if has(Section::Main) {
            return self.expected_at(span, "const, def, val or end of file");
        }
        if section == Section::Main && has_gfx {
            return self.expected_at(
                span,
                "const, def, val, gfx_init, gfx_frame, gfx_cleanup, gfx_key_down or gfx_main",
            );
        }
        let main = matches!(section, Section::Main | Section::GfxMain);
        if main && (has(Section::Main) || has(Section::GfxMain)) {
            return Self::fail(
                span.start,
                format!("there is already a {} block", section.word()),
            );
        }
        if has(section) {
            return Self::fail(
                span.start,
                format!("there is already a {} block", section.word()),
            );
        }
        self.seen.push(section);
        self.program.is_gfx |= section.is_gfx();
        self.section = Some(section);
        match section {
            Section::GfxFrame => self.program.delta_time_slot = Some(self.allocate()),
            Section::GfxKeyDown => self.program.key_slot = Some(self.allocate()),
            Section::Global
            | Section::Main
            | Section::GfxInit
            | Section::GfxCleanup
            | Section::GfxMain => {}
        }
        let checked = self.block(block);
        self.section = None;
        let statements = checked?;
        let end = self.next_token(after).0;
        let sections = &mut self.program.sections;
        match section {
            Section::Global => sections.global = statements,
            Section::Main | Section::GfxMain => {
                sections.main = Some((statements, end));
            }
            Section::GfxInit => sections.init = statements,
            Section::GfxFrame => sections.frame = statements,
            Section::GfxKeyDown => sections.key_down = statements,
            Section::GfxCleanup => sections.cleanup = statements,
        }
        Ok(())
    }

    fn types(&self, spans: &[Span], too_many: &str) -> Result<Vec<Kind>, Diagnostic> {
        let mut kinds = Vec::new();
        for span in spans {
            let text = self.text(*span);
            let Some((index, kind)) = split_indexed(text) else {
                return self.expected_at(
                    *span,
                    "a type with its index, like 0_i32, of i32, i64, u32, u64, f32, f64, flags, color, str or bool",
                );
            };
            if kinds.len() == FUNCTION_VALUES_MAX {
                return Self::fail(span.start, too_many);
            }
            if index != kinds.len() {
                return Self::fail(
                    span.start,
                    format!(
                        "the values are numbered from 0 in order, so '{text}' is {}_{}",
                        kinds.len(),
                        kind.name()
                    ),
                );
            }
            kinds.push(kind);
        }
        Ok(kinds)
    }

    fn def(
        &mut self,
        public: Option<Span>,
        (span, name): Named<'input>,
        parameters: &[Named<'input>],
        results: &(Span, Results),
        block: &[ast::Statement<'input>],
    ) -> Result<(), Diagnostic> {
        let is_public = self.public(public)?;
        if self.is_reserved(name) {
            return self.expected_at(span, "a function name");
        }
        let shown = self.text(span);
        if !is_function_name(name) {
            if name == "_fn" {
                return Self::fail(
                    span.start,
                    "the name of a function has more before _fn, as in double_fn",
                );
            }
            let base = shown
                .strip_prefix("fn_")
                .filter(|rest| !rest.is_empty())
                .unwrap_or(shown);
            return Self::fail(
                span.start,
                format!("the name of a function ends with _fn, as in {base}_fn"),
            );
        }
        if let Some(i) = self.function_any(name) {
            return Self::fail(
                span.start,
                format!("{} '{shown}' already exists", self.functions[i].noun()),
            );
        }
        if self.constant_any(name).is_some() {
            return Self::fail(span.start, format!("'{shown}' is already a constant"));
        }
        if self.variable_any(name).is_some() {
            return Self::fail(span.start, format!("'{shown}' is already a variable"));
        }
        let outer_variables = mem::take(&mut self.variables);
        let outer_slot_count = mem::replace(&mut self.slot_count, 0);
        let outer_section = self.section.take();
        let checked = self.function_body((span, name), is_public, parameters, results, block);
        self.section = outer_section;
        self.slot_count = outer_slot_count;
        self.variables = outer_variables;
        self.function = None;
        checked
    }

    fn function_body(
        &mut self,
        (span, name): Named<'input>,
        public: bool,
        parameters: &[Named<'input>],
        (_, results): &(Span, Results),
        block: &[ast::Statement<'input>],
    ) -> Result<(), Diagnostic> {
        let mut kinds = Vec::new();
        for parameter in parameters {
            let Some(kind) = parameter_kind(parameter.1) else {
                return self.expected_at(
                    parameter.0,
                    "a parameter whose name ends with its type, like count_i32",
                );
            };
            if kinds.len() == FUNCTION_VALUES_MAX {
                return Self::fail(
                    parameter.0.start,
                    format!("a function takes at most {FUNCTION_VALUES_MAX} values"),
                );
            }
            self.new_name(*parameter)?;
            let slot = self.allocate();
            self.declare(*parameter, kind, slot, true);
            kinds.push(kind);
        }
        let result_kinds = match results {
            Results::Void => Vec::new(),
            Results::Indexed(spans) => self.types(
                spans,
                &format!("a function returns at most {FUNCTION_VALUES_MAX} values"),
            )?,
        };
        let code = self.program.functions.len();
        self.program.functions.push(program::Function {
            name: name.to_owned(),
            slot_count: 0,
            statements: Vec::new(),
            returns_values: !result_kinds.is_empty(),
            end: span.start,
        });
        let declaration = self.declaration(span, public);
        self.function = Some(self.functions.len());
        self.functions.push(Function {
            name,
            callable: Callable::Function(code),
            parameters: kinds,
            results: result_kinds,
            declaration,
        });
        let statements = self.block(block)?;
        self.check_variables_used(0)?;
        if let Some(function) = self.program.functions.get_mut(code) {
            function.statements = statements;
            function.slot_count = self.slot_count;
        }
        Ok(())
    }

    fn array(
        &mut self,
        public: Option<Span>,
        (span, name): Named<'input>,
        kind_spans: &[Span],
        rows: &[(Span, Vec<ast::Operand<'input>>)],
    ) -> Result<(), Diagnostic> {
        let is_public = self.public(public)?;
        self.new_name((span, name))?;
        let kinds = self.array_kinds(kind_spans)?;
        let table = self.literal_rows(span, &kinds, rows, false)?;
        let index = self.program.arrays.len();
        self.program.arrays.push(Array {
            name: self.text(span).to_owned(),
            rows: table,
        });
        let declaration = self.declaration(span, is_public);
        self.functions.push(Function {
            name,
            callable: Callable::Array(index),
            parameters: vec![Kind::I32],
            results: kinds,
            declaration,
        });
        Ok(())
    }

    fn array_kinds(&self, spans: &[Span]) -> Result<Vec<Kind>, Diagnostic> {
        self.types(
            spans,
            &format!("a row of an array has at most {FUNCTION_VALUES_MAX} values"),
        )
    }

    fn check_array_size(
        &self,
        span: Span,
        name: Span,
        rows: usize,
        kinds: &[Kind],
    ) -> Result<(), Diagnostic> {
        let values = rows.saturating_mul(kinds.len());
        if values <= ARRAY_VALUES_MAX {
            return Ok(());
        }
        Self::fail(
            span.start,
            format!(
                "'{}' would hold {values} values, and an array holds at most {ARRAY_VALUES_MAX}",
                self.text(name)
            ),
        )
    }

    fn literal_rows(
        &mut self,
        span: Span,
        kinds: &[Kind],
        rows: &[(Span, Vec<ast::Operand<'input>>)],
        is_var: bool,
    ) -> Result<Vec<Vec<Value>>, Diagnostic> {
        let shown = self.text(span);
        let types = values_shown(kinds);
        let context = format!("a row of '{shown}'");
        let mut table = Vec::new();
        for (row_span, values) in rows {
            let mut row = Vec::new();
            for (index, kind) in kinds.iter().enumerate() {
                let Some(value) = values.get(index) else {
                    return Self::fail(
                        row_span.start,
                        format!("'{shown}' has rows of {types}, but this row has {index}"),
                    );
                };
                let not_literal = Diagnostic::new(
                    value.span.start,
                    format!(
                        "the values of a row are literals, like 7_i32, so a row cannot have '{}'",
                        self.text(value.span)
                    ),
                );
                if let Term::Name(word) = value.term
                    && !self.is_reserved(word)
                    && self.constant(word).is_none()
                {
                    self.check_not_hidden(value.span, word)?;
                    return Err(not_literal);
                }
                let Operand::Literal(literal) = self.value_of_kind(value, *kind, &context)? else {
                    return Err(not_literal);
                };
                row.push(literal);
            }
            if values.len() > kinds.len() {
                return Self::fail(
                    row_span.start,
                    format!("'{shown}' has rows of {types}, but this row has more"),
                );
            }
            table.push(row);
            if is_var {
                self.check_array_size(*row_span, span, table.len(), kinds)?;
            }
        }
        Ok(table)
    }

    fn var_array(
        &mut self,
        (span, name): Named<'input>,
        mutable: bool,
        array: Span,
        kind_spans: &[Span],
        rows: &Rows<'input>,
    ) -> Result<Statement, Diagnostic> {
        if !mutable {
            return Self::fail(
                array.start,
                "a val array is top-level, and an array of a block is a var",
            );
        }
        self.new_name((span, name))?;
        let kinds = self.array_kinds(kind_spans)?;
        let (values, repeat) = match rows {
            Rows::Literal(literal) => {
                let table = self.literal_rows(span, &kinds, literal, true)?;
                (table.into_iter().flatten().collect::<Vec<_>>(), 1)
            }
            Rows::Zeroed(keyword, count) => {
                let above_zero = match self.value_of_kind(count, Kind::I32, "with_len_col")? {
                    Operand::Literal(Value::I32(given)) => {
                        usize::try_from(given).ok().filter(|n| *n > 0)
                    }
                    Operand::Literal(_) | Operand::Slot(_) => None,
                };
                let Some(row_count) = above_zero else {
                    return Self::fail(
                        count.span.start,
                        "with_len_col needs an i32 literal or constant above zero, as the size of an array is known before the program runs",
                    );
                };
                self.check_array_size(count.span, span, row_count, &kinds)?;
                let mut zero = Vec::new();
                for kind in &kinds {
                    zero.push(match kind {
                        Kind::I32 => Value::I32(0),
                        Kind::I64 => Value::I64(0),
                        Kind::U32 => Value::U32(0),
                        Kind::U64 => Value::U64(0),
                        Kind::F32 => Value::F32(0.0),
                        Kind::F64 => Value::F64(0.0),
                        Kind::Flags => Value::Flags(0),
                        Kind::Color => Value::Color(0),
                        Kind::Str => Value::Str(String::new()),
                        Kind::Bool => Value::Bool(false),
                        Kind::InputKey | Kind::Font | Kind::Icon => {
                            return Self::fail(
                                keyword.start,
                                format!(
                                    "a zeroed row cannot have {}, which has no zero, but literal rows can",
                                    kind.name()
                                ),
                            );
                        }
                    });
                }
                (zero, row_count)
            }
        };
        let columns = kinds.len();
        let row_count = values.len().checked_div(columns).unwrap_or(0) * repeat;
        let slot = self.slot_count;
        self.slot_count += row_count * columns;
        let index = self.program.var_arrays.len();
        self.program.var_arrays.push(VarArray {
            name: self.text(span).to_owned(),
            slot,
            columns,
            rows: row_count,
        });
        self.declare((span, name), Kind::I32, slot, true);
        if let Some(variable) = self.variables.last_mut() {
            variable.array = Some((index, kinds));
        }
        Ok(Statement::ArrayInit {
            slot,
            values,
            repeat,
        })
    }

    fn set_at(
        &mut self,
        (span, name): Named<'input>,
        index: &ast::Operand<'input>,
        (operation_span, operation): (Span, Operation),
        values: &[ast::Operand<'input>],
    ) -> Result<Statement, Diagnostic> {
        let found = self
            .variable(name)
            .and_then(|i| self.variables[i].array.clone());
        let Some((array, kinds)) = found else {
            self.target((span, name))?;
            return self.expected(
                span.end,
                "to, to_add, to_subtract, to_multiply, to_divide, to_mod or a conversion",
            );
        };
        let shown = self.text(span);
        let index_operand =
            self.value_of_kind(index, Kind::I32, &format!("the index of '{shown}'"))?;
        if operation != Operation::Copy {
            return self.expected_at(operation_span, "to");
        }
        let types = values_shown(&kinds);
        let context = format!("a row of '{shown}'");
        let mut operands = Vec::new();
        for (i, kind) in kinds.iter().enumerate() {
            let Some(value) = values.get(i) else {
                return Self::fail(
                    span.start,
                    format!("'{shown}' has rows of {types}, but this row has {i}"),
                );
            };
            operands.push(self.value_of_kind(value, *kind, &context)?);
        }
        if let Some(extra) = values.get(kinds.len()) {
            return Self::fail(
                extra.span.start,
                format!("'{shown}' has rows of {types}, but this row has more"),
            );
        }
        Ok(Statement::ArrayWrite {
            array,
            index: index_operand,
            values: operands,
            offset: span.start,
        })
    }

    fn check_variables_used(&self, first: usize) -> Result<(), Diagnostic> {
        match self
            .variables
            .get(first..)
            .unwrap_or_default()
            .iter()
            .find(|v| !v.declaration.used)
        {
            Some(unused) => Self::fail(
                unused.declaration.span.start,
                format!("'{}' is never read", self.text(unused.declaration.span)),
            ),
            None => Ok(()),
        }
    }

    fn check_all_used(&self) -> Result<(), Diagnostic> {
        if let Some(unused) = self.constants.iter().find(|c| !c.declaration.used) {
            return Self::fail(
                unused.declaration.span.start,
                format!("'{}' is never used", self.text(unused.declaration.span)),
            );
        }
        if let Some(unused) = self.functions.iter().find(|f| !f.declaration.used) {
            let what = match unused.callable {
                Callable::Function(_) => "called",
                Callable::Array(_) | Callable::VarArray(_) => "read",
            };
            return Self::fail(
                unused.declaration.span.start,
                format!("'{}' is never {what}", self.text(unused.declaration.span)),
            );
        }
        self.check_variables_used(0)
    }

    fn block(&mut self, block: &[ast::Statement<'input>]) -> Result<Vec<Statement>, Diagnostic> {
        self.depth += 1;
        let visible = self.variables.len();
        let mut statements = Vec::new();
        for statement in block {
            match self.statement(statement) {
                Ok(Some(checked)) => statements.push(checked),
                Ok(None) => {}
                Err(diagnostic) => {
                    self.depth -= 1;
                    return Err(diagnostic);
                }
            }
        }
        self.depth -= 1;
        let shared =
            self.section == Some(Section::Global) && self.depth == 0 && self.function.is_none();
        if !shared {
            self.check_variables_used(visible)?;
            self.variables.truncate(visible);
        }
        Ok(statements)
    }

    fn in_main(&self) -> bool {
        matches!(self.section, Some(Section::Main | Section::GfxMain))
    }

    fn statement(
        &mut self,
        statement: &ast::Statement<'input>,
    ) -> Result<Option<Statement>, Diagnostic> {
        Ok(Some(match statement {
            ast::Statement::Info(values) => Statement::Info(
                values
                    .iter()
                    .map(|value| self.value(value).map(|(operand, _)| operand))
                    .collect::<Result<_, _>>()?,
            ),
            ast::Statement::Set {
                name,
                operation,
                values,
            } => self.set(*name, *operation, values)?,
            ast::Statement::SetConversion {
                name,
                conversion,
                values,
            } => self.set_conversion(*name, *conversion, values)?,
            ast::Statement::SetCall {
                name,
                operation,
                call,
            } => self.set_call(*name, *operation, call)?,
            ast::Statement::Declare {
                name,
                mutable,
                initial,
            } => self.declaration_statement(*name, *mutable, initial)?,
            ast::Statement::DeclareArray {
                name,
                mutable,
                array,
                kinds,
                rows,
            } => self.var_array(*name, *mutable, *array, kinds, rows)?,
            ast::Statement::SetAt {
                name,
                index,
                operation,
                values,
            } => self.set_at(*name, index, *operation, values)?,
            ast::Statement::If {
                branches,
                otherwise,
            } => {
                let mut checked = Vec::new();
                for (condition, block) in branches {
                    let holds = self.condition(condition)?;
                    checked.push((holds, self.block(block)?));
                }
                Statement::If {
                    branches: checked,
                    otherwise: otherwise
                        .as_ref()
                        .map(|block| self.block(block))
                        .transpose()?,
                }
            }
            ast::Statement::While(_, condition, block) => {
                let holds = self.condition(condition)?;
                Statement::While(holds, self.block(block)?)
            }
            ast::Statement::AwaitNextFrame(span) => {
                if self.section != Some(Section::GfxMain) || self.function.is_some() {
                    return Self::fail(
                        span.start,
                        format!("{} can only be in gfx_main", self.text(*span)),
                    );
                }
                Statement::AwaitNextFrame
            }
            ast::Statement::Draw {
                span,
                draw,
                aligns,
                values,
            } => self.draw(*span, draw, aligns.as_ref(), values)?,
            ast::Statement::Nihil => return Ok(None),
            ast::Statement::Return(span, status) => self.return_status(*span, *status)?,
            ast::Statement::ReturnValues(span, values) => self.return_values(*span, values)?,
        }))
    }

    fn set_conversion(
        &mut self,
        name: Named<'input>,
        conversion: (Span, Conversion),
        values: &[ast::Operand<'input>],
    ) -> Result<Statement, Diagnostic> {
        let (slot, kind) = self.target(name)?;
        let (converted, converted_kind) = self.conversion(slot, conversion, values)?;
        if converted_kind != kind {
            return Self::fail(
                conversion.0.start,
                format!(
                    "'{}' is {}, but the conversion gives {}",
                    self.text(name.0),
                    kind.name(),
                    converted_kind.name()
                ),
            );
        }
        Ok(converted)
    }

    fn set_call(
        &mut self,
        name: Named<'input>,
        (_, operation): (Span, Operation),
        call: &ast::Call<'input>,
    ) -> Result<Statement, Diagnostic> {
        let (slot, kind) = self.target(name)?;
        if operation != Operation::Copy {
            return self.fail_unknown_variable(call.name.0, call.name.1);
        }
        let (checked, result_kind, gives) = self.call(call)?;
        if result_kind != kind {
            return Self::fail(
                name.0.start,
                format!(
                    "'{}' is {}, but '{}' {gives} {}",
                    self.text(name.0),
                    kind.name(),
                    self.text(call.name.0),
                    result_kind.name()
                ),
            );
        }
        Ok(Statement::Call {
            slot,
            call: checked,
        })
    }

    fn return_status(&self, span: Span, status: ExitStatus) -> Result<Statement, Diagnostic> {
        if self.function.is_some() {
            let (at, _) = self.next_token(span.end);
            return Self::fail(
                at,
                "only bosonoga_main and gfx_main return an exit status, a function returns its values",
            );
        }
        if !self.in_main() {
            return Self::fail(span.start, "only bosonoga_main and gfx_main can return");
        }
        Ok(Statement::Return(status))
    }

    fn return_values(
        &mut self,
        span: Span,
        values: &[ast::Operand<'input>],
    ) -> Result<Statement, Diagnostic> {
        if let Some(index) = self.function {
            return self.function_return(index, span, values);
        }
        if !self.in_main() {
            return Self::fail(span.start, "only bosonoga_main and gfx_main can return");
        }
        self.expected(span.end, "exit_success or exit_failure")
    }

    fn function_return(
        &mut self,
        index: usize,
        span: Span,
        values: &[ast::Operand<'input>],
    ) -> Result<Statement, Diagnostic> {
        let Some(function) = self.functions.get(index) else {
            return Ok(Statement::ReturnValues(Vec::new()));
        };
        let results = function.results.clone();
        let name = function.name;
        let shown = values_shown(&results);
        let mut operands = Vec::new();
        for (i, kind) in results.iter().enumerate() {
            let Some(value) = values.get(i) else {
                return Self::fail(
                    span.start,
                    format!("'{name}' returns {shown}, but this return has {i}"),
                );
            };
            operands.push(self.value_of_kind(value, *kind, "return")?);
        }
        if values.len() > results.len() {
            return Self::fail(
                span.start,
                format!("'{name}' returns {shown}, but this return has more"),
            );
        }
        Ok(Statement::ReturnValues(operands))
    }

    fn declaration_statement(
        &mut self,
        name: Named<'input>,
        mutable: bool,
        initial: &Initial<'input>,
    ) -> Result<Statement, Diagnostic> {
        self.new_name(name)?;
        let (statement, kind, slot) = match initial {
            Initial::Value { value, shift_or } => {
                let (first, kind) = self.value(value)?;
                let mut flags = Vec::new();
                for (word, flag) in shift_or {
                    if kind != Kind::Flags {
                        return Self::fail(word.start, "expected a statement, got 'shift_or'");
                    }
                    flags.push(self.value_of_kind(flag, Kind::Flags, "shift_or")?);
                }
                let slot = self.allocate();
                (
                    Statement::Assign {
                        slot,
                        initial: first,
                        shift_or: flags,
                    },
                    kind,
                    slot,
                )
            }
            Initial::Conversion(conversion, values) => {
                let slot = self.allocate();
                let (statement, kind) = self.conversion(slot, *conversion, values)?;
                (statement, kind, slot)
            }
            Initial::Call(call) => {
                let (checked, kind, _) = self.call(call)?;
                let slot = self.allocate();
                (
                    Statement::Call {
                        slot,
                        call: checked,
                    },
                    kind,
                    slot,
                )
            }
        };
        self.declare(name, kind, slot, mutable);
        Ok(statement)
    }

    fn target(&self, (span, name): Named<'input>) -> Result<(usize, Kind), Diagnostic> {
        if self.is_reserved(name) {
            return self.expected_at(span, "a variable name");
        }
        let shown = self.text(span);
        if self.constant(name).is_some() {
            return Self::fail(
                span.start,
                format!("'{shown}' is a constant and cannot be set"),
            );
        }
        let Some(index) = self.variable(name) else {
            if let Some(i) = self.function(name)
                && matches!(self.functions[i].callable, Callable::Array(_))
            {
                return Self::fail(
                    span.start,
                    format!(
                        "'{shown}' is a val array and cannot be set, but a var array of a block can"
                    ),
                );
            }
            return self.fail_unknown_variable(span, name);
        };
        let variable = &self.variables[index];
        if variable.array.is_some() {
            return Self::fail(
                span.start,
                format!(
                    "'{shown}' is an array, whose rows are set with set {shown} at INDEX to and one value per column"
                ),
            );
        }
        if !variable.mutable {
            return Self::fail(span.start, format!("'{shown}' is a val and cannot be set"));
        }
        Ok((variable.slot, variable.kind))
    }

    fn set(
        &mut self,
        name: Named<'input>,
        (span, operation): (Span, Operation),
        values: &[ast::Operand<'input>],
    ) -> Result<Statement, Diagnostic> {
        let (slot, kind) = self.target(name)?;
        let shown = self.text(name.0);
        if operation == Operation::Copy {
            let mut given = values.iter();
            let Some(first) = given.next() else {
                return self.expected(span.end, "a value");
            };
            let initial = self.value_of_kind(first, kind, "set ... to")?;
            if let Some(extra) = given.next() {
                return Self::fail(
                    extra.span.start,
                    format!("set ... to sets '{shown}' to a single value"),
                );
            }
            return Ok(Statement::Assign {
                slot,
                initial,
                shift_or: Vec::new(),
            });
        }
        let word = self.text(span);
        if !kind.is_number() {
            return Self::fail(
                span.start,
                format!(
                    "'{shown}' is {}, which cannot be used in arithmetic, but set ... to sets it",
                    kind.name()
                ),
            );
        }
        let mut operands = Vec::new();
        for value in values {
            operands.push(self.value_of_kind(value, kind, word)?);
        }
        if operands.len() == 1 {
            if operation == Operation::Modulo {
                let after = values.first().map_or(span.end, |value| value.span.end);
                return self.expected(after, "a value to divide by");
            }
            if let Some(identity) = operation.identity(kind) {
                operands.insert(0, Operand::Literal(identity));
            }
        }
        Ok(Statement::Calculate {
            slot,
            operation,
            values: operands,
            offset: span.start,
        })
    }

    fn conversion(
        &mut self,
        slot: usize,
        (span, conversion): (Span, Conversion),
        values: &[ast::Operand<'input>],
    ) -> Result<(Statement, Kind), Diagnostic> {
        let word = conversion.word();
        let mut given = values.iter();
        let Some(first) = given.next() else {
            return self.expected(span.end, "a value");
        };
        let (left, source) = self.value(first)?;
        let accepts_integer = conversion.accepts_integer();
        if !(source.is_real() || (accepts_integer && source.is_integer())) {
            return Self::fail(
                first.span.start,
                format!(
                    "{word} needs {}, got {}",
                    if accepts_integer {
                        "a number"
                    } else {
                        "f32 or f64"
                    },
                    source.name()
                ),
            );
        }
        let right = if conversion.is_binary() {
            let Some(second) = given.next() else {
                return self.expected(first.span.end, "a value");
            };
            Some(self.value_of_kind(second, source, word)?)
        } else {
            None
        };
        if let Some(extra) = given.next() {
            return self.expected_at(extra.span, "a statement");
        }
        Ok((
            Statement::Convert {
                slot,
                conversion,
                left,
                right,
                offset: span.start,
            },
            conversion.result_kind(source),
        ))
    }

    fn use_function(&mut self, index: usize) {
        if self.function != Some(index)
            && let Some(function) = self.functions.get_mut(index)
        {
            function.declaration.used = true;
        }
    }

    fn arguments(
        &mut self,
        call: &ast::Call<'input>,
        callable: Callable,
        parameters: &[Kind],
    ) -> Result<(Callee, Vec<Operand>), Diagnostic> {
        let (span, _) = call.name;
        let shown = self.text(span);
        Ok(match callable {
            Callable::Array(_) | Callable::VarArray(_) => {
                let index_operand = match &call.arguments {
                    Arguments::At(_, operand) => {
                        self.value_of_kind(operand, Kind::I32, &format!("the index of '{shown}'"))?
                    }
                    Arguments::Of(of, _) => {
                        return Self::fail(
                            of.start,
                            format!(
                                "'{shown}' is an array, which is read at one i32 index, as in {shown} at 0_i32"
                            ),
                        );
                    }
                    Arguments::None => {
                        let (at, _) = self.next_token(span.end);
                        return Self::fail(
                            at,
                            format!(
                                "'{shown}' is an array, which is read at one i32 index, as in {shown} at 0_i32"
                            ),
                        );
                    }
                };
                let callee = if let Callable::VarArray(array) = callable {
                    Callee::VarArray(array)
                } else if let Callable::Array(array) = callable {
                    Callee::Array(array)
                } else {
                    return Ok((Callee::Function(0), Vec::new()));
                };
                (callee, vec![index_operand])
            }
            Callable::Function(code) => {
                let taken = values_shown(parameters);
                let values = match &call.arguments {
                    Arguments::At(at, _) => {
                        return Self::fail(
                            at.start,
                            format!(
                                "'{shown}' is a function, and only an array is read at an index"
                            ),
                        );
                    }
                    Arguments::Of(of, _) if parameters.is_empty() => {
                        return Self::fail(
                            of.start,
                            format!("'{shown}' takes no values, so its call has no of"),
                        );
                    }
                    Arguments::None if !parameters.is_empty() => {
                        let (at, _) = self.next_token(span.end);
                        return Self::fail(
                            at,
                            format!(
                                "'{shown}' takes {taken}, which follow of, as in {shown} of ..."
                            ),
                        );
                    }
                    Arguments::Of(_, values) => values.as_slice(),
                    Arguments::None => &[],
                };
                let mut operands = Vec::new();
                for (i, kind) in parameters.iter().enumerate() {
                    let Some(value) = values.get(i) else {
                        let after = values.last().map_or(span.end, |value| value.span.end);
                        let (at, _) = self.next_token(after);
                        return Self::fail(
                            at,
                            format!("'{shown}' takes {taken}, but is given {i}"),
                        );
                    };
                    operands.push(self.value_of_kind(value, *kind, shown)?);
                }
                if let Some(extra) = values.get(parameters.len()) {
                    return Self::fail(
                        extra.span.start,
                        format!("'{shown}' takes {taken}, but is given more"),
                    );
                }
                (Callee::Function(code), operands)
            }
        })
    }

    fn call(
        &mut self,
        call: &ast::Call<'input>,
    ) -> Result<(program::Call, Kind, &'static str), Diagnostic> {
        let (span, name) = call.name;
        let shown = self.text(span);
        let var_array = self.variable(name).and_then(|i| {
            let variable = &mut self.variables[i];
            let (array, kinds) = variable.array.clone()?;
            variable.declaration.used = true;
            Some((
                Callable::VarArray(array),
                vec![Kind::I32],
                kinds,
                "has rows of",
            ))
        });
        let (callable, parameters, results, gives) = if let Some(found) = var_array {
            found
        } else {
            let Some(index) = self.function(name) else {
                if self.variable(name).is_some() {
                    return self.expected(span.end, "a statement");
                }
                return self.fail_unknown_variable(span, name);
            };
            self.use_function(index);
            let function = &self.functions[index];
            (
                function.callable,
                function.parameters.clone(),
                function.results.clone(),
                function.gives(),
            )
        };
        let (callee, arguments) = self.arguments(call, callable, &parameters)?;
        let Some(&result) = results.first().filter(|_| results.len() == 1) else {
            if results.is_empty() {
                return Self::fail(
                    span.start,
                    format!("'{shown}' returns no values, so it is called with call"),
                );
            }
            return Self::fail(
                span.start,
                format!(
                    "'{shown}' {gives} {}, which call ... with ret takes for its block, each named, as in with ret 0_{} as value",
                    kinds_named(&results),
                    results.first().map_or("i32", |kind| kind.name())
                ),
            );
        };
        let text = self.text(call.taken);
        if split_indexed(text) != Some((0, result)) {
            return Self::fail(
                call.taken.start,
                format!(
                    "'{shown}' {gives} {}, so its call ends with ret 0_{}, not '{text}'",
                    result.name(),
                    result.name()
                ),
            );
        }
        Ok((
            program::Call {
                callee,
                arguments,
                offset: span.start,
            },
            result,
            gives,
        ))
    }

    fn draw(
        &mut self,
        span: Span,
        draw: &ast::Draw,
        aligns: Option<&(Named<'input>, Named<'input>)>,
        values: &[ast::Operand<'input>],
    ) -> Result<Statement, Diagnostic> {
        let word = self.text(span);
        if self.section != Some(Section::GfxFrame) || self.function.is_some() {
            return Self::fail(span.start, format!("{word} can only be in gfx_frame"));
        }
        let kinds: &[Kind] = match draw {
            ast::Draw::Clear => &[Kind::Color],
            ast::Draw::Rectangle => &[Kind::F32, Kind::F32, Kind::F32, Kind::F32, Kind::Color],
            ast::Draw::Hex | ast::Draw::Circle => &[Kind::F32, Kind::F32, Kind::F32, Kind::Color],
            ast::Draw::Icon => &[Kind::F32, Kind::F32, Kind::Icon, Kind::F32, Kind::Color],
            ast::Draw::Text => &[Kind::F32, Kind::F32, Kind::Font, Kind::F32, Kind::Color],
        };
        let align = match aligns {
            Some(((h_span, h), (v_span, v))) => {
                let horizontal = match *h {
                    "left" => Horizontal::Left,
                    "center" => Horizontal::Center,
                    "right" => Horizontal::Right,
                    _ => return self.expected_at(*h_span, "left, center or right"),
                };
                let vertical = match *v {
                    "top" => Vertical::Top,
                    "middle" => Vertical::Middle,
                    "baseline" => Vertical::Baseline,
                    "bottom" => Vertical::Bottom,
                    _ => return self.expected_at(*v_span, "top, middle, baseline or bottom"),
                };
                Align {
                    horizontal,
                    vertical,
                }
            }
            None => Align {
                horizontal: Horizontal::Left,
                vertical: Vertical::Baseline,
            },
        };
        let mut operands = Vec::new();
        for (value, kind) in values.iter().zip(kinds) {
            operands.push(self.value_of_kind(value, *kind, word)?);
        }
        let mut parts = Vec::new();
        for value in values.iter().skip(kinds.len()) {
            parts.push(self.value(value)?.0);
        }
        let mut checked_values = operands.into_iter();
        let mut next = || {
            checked_values
                .next()
                .unwrap_or(Operand::Literal(Value::F32(0.0)))
        };
        let checked = match draw {
            ast::Draw::Clear => Draw::Clear(next()),
            ast::Draw::Rectangle => Draw::Rectangle([next(), next(), next(), next(), next()]),
            ast::Draw::Hex => Draw::Hex([next(), next(), next(), next()]),
            ast::Draw::Circle => Draw::Circle([next(), next(), next(), next()]),
            ast::Draw::Icon => Draw::Icon([next(), next(), next(), next(), next()]),
            ast::Draw::Text => Draw::Text {
                align,
                values: [next(), next(), next(), next(), next()],
                parts,
            },
        };
        Ok(Statement::Draw {
            draw: checked,
            offset: span.start,
        })
    }

    fn value_of_kind(
        &mut self,
        operand: &ast::Operand<'input>,
        kind: Kind,
        context: &str,
    ) -> Result<Operand, Diagnostic> {
        let (checked, actual) = self.value(operand)?;
        if actual != kind {
            return Self::fail(
                operand.span.start,
                format!(
                    "{context} needs {}, but '{}' is {}",
                    kind.name(),
                    self.text(operand.span),
                    actual.name()
                ),
            );
        }
        Ok(checked)
    }

    fn value(&mut self, operand: &ast::Operand<'input>) -> Result<(Operand, Kind), Diagnostic> {
        let at = operand.span.start;
        let text = self.text(operand.span);
        match &operand.term {
            Term::Literal(value) => Ok((Operand::Literal(value.clone()), value.kind())),
            Term::Shift(word) => word
                .strip_prefix("shift_")
                .and_then(|digits| digits.parse::<u64>().ok())
                .filter(|bit| *bit <= SHIFT_BIT_MAX)
                .and_then(|bit| u32::try_from(bit).ok())
                .map(|bit| (Operand::Literal(Value::Flags(1 << bit)), Kind::Flags))
                .ok_or_else(|| {
                    Diagnostic::new(
                        at,
                        format!("'{text}' is out of range, the last bit is shift_{SHIFT_BIT_MAX}"),
                    )
                }),
            Term::Result(_) => Self::fail(
                at,
                format!(
                    "{text} is not a value, a call names the values that it takes, as in call ... with ret 0_i32 as count"
                ),
            ),
            Term::GfxValue(word) => {
                let in_function = self.function.is_some();
                if *word == "gfx_key" {
                    match self.program.key_slot {
                        Some(slot) if self.section == Some(Section::GfxKeyDown) && !in_function => {
                            Ok((Operand::Slot(slot), Kind::InputKey))
                        }
                        _ => Self::fail(at, "gfx_key can only be in gfx_key_down"),
                    }
                } else {
                    match self.program.delta_time_slot {
                        Some(slot) if self.section == Some(Section::GfxFrame) && !in_function => {
                            Ok((Operand::Slot(slot), Kind::F32))
                        }
                        _ => Self::fail(at, "gfx_delta_time can only be in gfx_frame"),
                    }
                }
            }
            Term::Name(name) => {
                if let Some(index) = self.constant(name) {
                    let constant = &mut self.constants[index];
                    constant.declaration.used = true;
                    return Ok((
                        Operand::Literal(constant.value.clone()),
                        constant.value.kind(),
                    ));
                }
                if let Some(value) = self.setting(name) {
                    let kind = value.kind();
                    return Ok((Operand::Literal(value), kind));
                }
                let Some(index) = self
                    .variable(name)
                    .filter(|&i| self.variables[i].array.is_none())
                else {
                    return self.fail_unknown_variable(operand.span, name);
                };
                let variable = &mut self.variables[index];
                variable.declaration.used = true;
                Ok((Operand::Slot(variable.slot), variable.kind))
            }
        }
    }

    fn condition(&mut self, condition: &ast::Condition<'input>) -> Result<Condition, Diagnostic> {
        let mut checked = Vec::new();
        for conjunction in condition {
            let mut comparisons = Vec::new();
            for comparison in conjunction {
                comparisons.push(self.comparison(comparison)?);
            }
            checked.push(comparisons);
        }
        Ok(checked)
    }

    fn comparison(
        &mut self,
        comparison: &ast::Comparison<'input>,
    ) -> Result<Comparison, Diagnostic> {
        let (left, left_kind) = self.value(&comparison.left)?;
        let (compare, right) = match &comparison.right {
            Some((span, compare, value)) => {
                let (right, right_kind) = self.value(value)?;
                self.check_compare(*span, *compare, left_kind, right_kind)?;
                (*compare, right)
            }
            None if left_kind == Kind::Bool => (Compare::Eq, Operand::Literal(Value::Bool(true))),
            None => return self.expected(comparison.left.span.end, "a comparison"),
        };
        Ok(Comparison {
            negate: comparison.negate,
            left,
            compare,
            right,
        })
    }

    fn check_compare(
        &self,
        span: Span,
        compare: Compare,
        left: Kind,
        right: Kind,
    ) -> Result<(), Diagnostic> {
        let word = self.text(span);
        let fail = |message: String| Self::fail(span.start, message);
        if compare == Compare::ShiftHas {
            if left == Kind::Flags && right == Kind::Flags {
                return Ok(());
            }
            return fail(format!(
                "shift_has needs flags, not {} and {}",
                left.name(),
                right.name()
            ));
        }
        if left != right {
            return fail(format!(
                "cannot compare {} with {}",
                left.name(),
                right.name()
            ));
        }
        if left == Kind::Flags {
            return fail("flags can only be compared with shift_has".to_owned());
        }
        if left == Kind::Color {
            return fail("colors cannot be compared".to_owned());
        }
        if matches!(
            left,
            Kind::Str | Kind::Bool | Kind::InputKey | Kind::Font | Kind::Icon
        ) && !matches!(compare, Compare::Eq | Compare::NotEq)
        {
            return fail(format!(
                "{} can only be compared with eq or not_eq",
                left.name()
            ));
        }
        match compare {
            Compare::Eq | Compare::NotEq if left.is_real() => fail(format!(
                "{} has no exact {word}, use {word}_3 or {word}_6",
                left.name()
            )),
            Compare::Eq3 | Compare::Eq6 | Compare::NotEq3 | Compare::NotEq6
                if left.is_integer() =>
            {
                fail(format!(
                    "{word} only works on f32 or f64, {} uses eq or not_eq",
                    left.name()
                ))
            }
            Compare::Gt
            | Compare::Lt
            | Compare::Ge
            | Compare::Le
            | Compare::Eq
            | Compare::NotEq
            | Compare::Eq3
            | Compare::Eq6
            | Compare::NotEq3
            | Compare::NotEq6
            | Compare::ShiftHas => Ok(()),
        }
    }
}
