use std::mem;

use super::{
    Callable, Checker, FUNCTION_VALUES_MAX, Function, Scope, Variable, is_function_name,
    split_indexed,
};
use crate::Diagnostic;
use crate::ast::{self, Item, Named, Results, Section, Span};
use crate::program::{self, Array, Operand};
use crate::value::Kind;

const OPTIONAL_TYPES: &str =
    "optional_ and i32, i64, u32, u64, f32, f64, bool, str or color, as in 0_optional_i32 or count_optional_i32";

fn parameter_kind(name: &str) -> Option<Kind> {
    let bytes = name.as_bytes();
    for start in 2..name.len() {
        if bytes[start - 1] != b'_' {
            continue;
        }
        let Some(suffix) = name.get(start..) else {
            continue;
        };
        // count_optional_flags is not a flags parameter, but a mistake.
        if suffix.starts_with("optional_") {
            return Kind::named(suffix);
        }
        if let Some(kind) = Kind::named(suffix) {
            return Some(kind);
        }
    }
    None
}

impl<'input> Checker<'_, 'input> {
    pub(super) fn items(&mut self, file: usize) -> Result<(), Diagnostic> {
        let files = self.files;
        let modules = self.modules;
        let Some(source) = files.get(file) else {
            return Ok(());
        };
        let mut next_module = 0;
        for item in &source.items {
            match item {
                Item::Mod(..) => {
                    let Some(list) = modules.get(file) else {
                        continue;
                    };
                    let Some(module) = list.get(next_module) else {
                        continue;
                    };
                    next_module += 1;
                    self.module(*module)?;
                }
                Item::Const {
                    public,
                    name,
                    value,
                } => self.constant_item(*public, *name, value)?,
                Item::Def {
                    public,
                    name,
                    parameters,
                    globals,
                    results,
                    block,
                } => self.def(*public, *name, parameters, globals, results, block)?,
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

    fn module(&mut self, module: usize) -> Result<(), Diagnostic> {
        let scope = self.scopes.len();
        self.scopes.push(Scope {
            file: module,
            parent: self.scope,
        });
        let outer = mem::replace(&mut self.scope, scope);
        self.items(module)?;
        self.scope = outer;
        Ok(())
    }

    fn constant_item(
        &mut self,
        public: Option<Span>,
        name: Named<'input>,
        value: &ast::Operand<'input>,
    ) -> Result<(), Diagnostic> {
        let is_public = self.public(public)?;
        self.new_name(name)?;
        let (checked, _) = self.value(value)?;
        let Operand::Literal(literal) = checked else {
            return Self::fail(
                value.span.start,
                "a constant needs a literal or an earlier constant",
            );
        };
        let declaration = self.declaration(name.0, is_public);
        self.constants.push(super::Constant {
            name: name.1,
            value: literal,
            declaration,
        });
        Ok(())
    }

    fn seen_gfx(&self) -> bool {
        for seen in &self.seen {
            if seen.is_gfx() {
                return true;
            }
        }
        false
    }

    fn section(
        &mut self,
        span: Span,
        section: Section,
        block: &[ast::Statement<'input>],
        after: usize,
    ) -> Result<(), Diagnostic> {
        let has_main = self.seen.contains(&Section::Main);
        let has_gfx = self.seen_gfx();
        if section == Section::Global
            && (has_main || has_gfx || self.seen.contains(&Section::Global))
        {
            return Self::fail(span.start, "bosonoga_global must be the first section");
        }
        if has_main {
            return self.expected_at(span, "const, def, val or end of file");
        }
        if section == Section::Main && has_gfx {
            return self.expected_at(
                span,
                "const, def, val, gfx_init, gfx_frame, gfx_cleanup, gfx_key_down or gfx_main",
            );
        }
        let is_main = section == Section::Main || section == Section::GfxMain;
        let has_any_main = has_main || self.seen.contains(&Section::GfxMain);
        if (is_main && has_any_main) || self.seen.contains(&section) {
            return Self::fail(
                span.start,
                format!("there is already a {} block", section.word()),
            );
        }
        self.seen.push(section);
        if section.is_gfx() {
            self.program.is_gfx = true;
        }
        self.section = Some(section);
        if section == Section::GfxFrame {
            self.program.delta_time_slot = Some(self.allocate());
        }
        if section == Section::GfxKeyDown {
            self.program.key_slot = Some(self.allocate());
        }
        let checked = self.block(block);
        self.section = None;
        let statements = checked?;
        let (end, _) = self.next_token(after);
        let sections = &mut self.program.sections;
        match section {
            Section::Global => sections.global = statements,
            Section::Main | Section::GfxMain => sections.main = Some((statements, end)),
            Section::GfxInit => sections.init = statements,
            Section::GfxFrame => sections.frame = statements,
            Section::GfxKeyDown => sections.key_down = statements,
            Section::GfxCleanup => sections.cleanup = statements,
        }
        Ok(())
    }

    pub(super) fn types(&self, spans: &[Span], too_many: &str) -> Result<Vec<Kind>, Diagnostic> {
        let mut kinds = Vec::new();
        for span in spans {
            let text = self.text(*span);
            let Some((index, kind)) = split_indexed(text) else {
                if text.contains("_optional_") {
                    return self.expected_at(*span, OPTIONAL_TYPES);
                }
                return self.expected_at(
                    *span,
                    "a type with its index, like 0_i32, of i32, i64, u32, u64, f32, f64, flags, color, str or bool, or optional_ and one of these but flags",
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
        globals: &[Named<'input>],
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
            let mut base = shown;
            if let Some(rest) = shown.strip_prefix("fn_")
                && !rest.is_empty()
            {
                base = rest;
            }
            return Self::fail(
                span.start,
                format!("the name of a function ends with _fn, as in {base}_fn"),
            );
        }
        if let Some(index) = self.function_any(name) {
            return Self::fail(
                span.start,
                format!("{} '{shown}' already exists", self.functions[index].noun()),
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
        let checked = self.function_body(
            (span, name),
            is_public,
            parameters,
            (globals, &outer_variables),
            &results.1,
            block,
        );
        // A global that the function reads is read, as its other uses are.
        let mut read = Vec::new();
        for variable in &self.variables {
            if variable.captured && variable.declaration.used {
                read.push(variable.name);
            }
        }
        self.section = outer_section;
        self.slot_count = outer_slot_count;
        self.variables = outer_variables;
        self.function = None;
        for global in read {
            if let Some(index) = self.variable_any(global) {
                self.variables[index].declaration.used = true;
            }
        }
        checked
    }

    // Declares a variable of bosonoga_global that with from_global lists, in
    // its first frame, which the function reads and sets.
    fn capture(
        &mut self,
        (span, name): Named<'input>,
        outer: &[Variable<'input>],
    ) -> Result<(), Diagnostic> {
        let shown = self.text(span);
        if self.is_reserved(name) {
            return self.expected_at(span, "a variable of bosonoga_global");
        }
        if let Some(index) = self.variable_any(name) {
            if self.variables[index].captured {
                return Self::fail(
                    span.start,
                    format!("'{shown}' is already in with from_global"),
                );
            }
            return Self::fail(
                span.start,
                format!("'{shown}' is a parameter, so it is not also in with from_global"),
            );
        }
        let mut found = None;
        for variable in outer {
            if variable.name == name
                && variable.slot.is_global()
                && self.visible(&variable.declaration)
            {
                found = Some(variable);
            }
        }
        let Some(global) = found else {
            let before = if self.seen.contains(&Section::Global) {
                ""
            } else {
                ", and with from_global needs bosonoga_global before the function"
            };
            return Self::fail(
                span.start,
                format!("'{shown}' is not a variable of bosonoga_global{before}"),
            );
        };
        let variable = Variable {
            name,
            kind: global.kind,
            slot: global.slot,
            mutable: global.mutable,
            array: global.array.clone(),
            declaration: self.declaration(span, false),
            captured: true,
            written: false,
        };
        self.variables.push(variable);
        Ok(())
    }

    fn function_body(
        &mut self,
        (span, name): Named<'input>,
        public: bool,
        parameters: &[Named<'input>],
        (globals, outer): (&[Named<'input>], &[Variable<'input>]),
        results: &Results,
        block: &[ast::Statement<'input>],
    ) -> Result<(), Diagnostic> {
        let mut kinds = Vec::new();
        for parameter in parameters {
            let Some(kind) = parameter_kind(parameter.1) else {
                if parameter.1.contains("_optional_") {
                    return self.expected_at(parameter.0, OPTIONAL_TYPES);
                }
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
        for global in globals {
            self.capture(*global, outer)?;
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

    pub(super) fn array_kinds(&self, spans: &[Span]) -> Result<Vec<Kind>, Diagnostic> {
        let kinds = self.types(
            spans,
            &format!("a row of an array has at most {FUNCTION_VALUES_MAX} values"),
        )?;
        for (span, kind) in spans.iter().zip(&kinds) {
            if kind.is_optional() {
                return Self::fail(
                    span.start,
                    format!("an array cannot have {}, but a variable can", kind.name()),
                );
            }
        }
        Ok(kinds)
    }
}
