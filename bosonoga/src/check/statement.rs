use super::{Checker, values_shown};
use crate::Diagnostic;
use crate::ast::{self, ExitStatus, Initial, Named, Section, Span};
use crate::io::Builtin;
use crate::program::{self, Operand, Slot, Statement};
use crate::value::{Compare, Conversion, Kind, Operation, Value};

impl<'input> Checker<'_, 'input> {
    pub(super) fn statement(
        &mut self,
        statement: &ast::Statement<'input>,
    ) -> Result<Option<Statement>, Diagnostic> {
        let checked = match statement {
            ast::Statement::Info(values) => Statement::Info(self.operands(values, "info")?),
            ast::Statement::Call {
                span,
                name,
                arguments,
                taking,
            } => self.call_statement(*span, *name, arguments, taking.as_ref())?,
            ast::Statement::Join(name, values) => {
                let (slot, kind) = self.target(*name)?;
                if kind != Kind::Str {
                    return Self::fail(name.0.start, "tu_join needs a str variable");
                }
                Statement::Join(slot, self.operands(values, "tu_join")?)
            }
            ast::Statement::Given {
                operation,
                values,
                results,
                block,
            } => self.given(*operation, values, results, block)?,
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
            } => self.if_statement(branches, otherwise.as_ref())?,
            ast::Statement::While(span, condition, block) => {
                let holds = self.condition(condition)?;
                Statement::While {
                    condition: holds,
                    block: self.block(block)?,
                    offset: span.start,
                }
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
                align,
                values,
            } => self.draw(*span, draw, *align, values)?,
            ast::Statement::Camera3d {
                span,
                values,
                block,
            } => self.camera_3d(*span, values, block)?,
            ast::Statement::MouseCapture(span, capture) => self.mouse_capture(*span, *capture)?,
            ast::Statement::Nihil => return Ok(None),
            ast::Statement::Return(span, status) => self.return_status(*span, *status)?,
            ast::Statement::ReturnValues(span, values) => self.return_values(*span, values)?,
        };
        Ok(Some(checked))
    }

    // gfx_mouse_capture or gfx_mouse_release.
    fn mouse_capture(&self, span: Span, capture: bool) -> Result<Statement, Diagnostic> {
        if !self.in_mouse_section() {
            return Self::fail(
                span.start,
                format!(
                    "{} can only be in gfx_frame, gfx_key_down or gfx_key_up",
                    self.text(span)
                ),
            );
        }
        Ok(Statement::MouseCapture {
            capture,
            offset: span.start,
        })
    }

    fn operands(
        &mut self,
        values: &[ast::Operand<'input>],
        word: &str,
    ) -> Result<Vec<Operand>, Diagnostic> {
        let mut operands = Vec::new();
        for value in values {
            operands.push(self.shown_value(value, word)?);
        }
        Ok(operands)
    }

    fn if_statement(
        &mut self,
        branches: &[(ast::Condition<'input>, ast::Block<'input>)],
        otherwise: Option<&ast::Block<'input>>,
    ) -> Result<Statement, Diagnostic> {
        let mut checked = Vec::new();
        for (condition, block) in branches {
            checked.push(self.branch(condition, block)?);
        }
        let mut checked_otherwise = None;
        if let Some(block) = otherwise {
            checked_otherwise = Some(self.block(block)?);
        }
        Ok(Statement::If {
            branches: checked,
            otherwise: checked_otherwise,
        })
    }

    // A branch of an if. Its condition can be some count as number alone,
    // which names the value of the optional count for the block.
    fn branch(
        &mut self,
        condition: &ast::Condition<'input>,
        block: &ast::Block<'input>,
    ) -> Result<(program::Condition, Vec<Statement>), Diagnostic> {
        let mut bound = None;
        if let [conjunction] = condition.as_slice()
            && let [comparison] = conjunction.as_slice()
            && let Some(presence) = &comparison.presence
            && let Some(binding) = presence.binding
            && !comparison.negate
        {
            bound = Some((comparison, presence.span, binding));
        }
        let Some((comparison, some, binding)) = bound else {
            let holds = self.condition(condition)?;
            return Ok((holds, self.block(block)?));
        };
        let (optional, inner) = self.optional_value(some, &comparison.left)?;
        let holds = vec![vec![program::Comparison {
            negate: false,
            left: optional.clone(),
            compare: Compare::NotEq,
            right: Operand::Literal(Value::Optional { inner, value: None }),
        }]];
        let visible = self.variables.len();
        self.new_name(binding)?;
        let slot = self.allocate();
        self.declare(binding, inner, slot, false);
        let mut statements = vec![Statement::Unwrap { slot, optional }];
        statements.extend(self.block(block)?);
        self.check_variables_used(visible)?;
        self.variables.truncate(visible);
        Ok((holds, statements))
    }

    fn given(
        &mut self,
        (span, builtin): (Span, Builtin),
        values: &[ast::Operand<'input>],
        results: &[(Span, Named<'input>)],
        block: &ast::Block<'input>,
    ) -> Result<Statement, Diagnostic> {
        let word = self.text(span);
        let (parameters, kinds) = builtin.signature();
        let uses_directory = builtin.uses_directory();
        if values.len() != parameters.len() + usize::from(uses_directory) {
            return Self::fail(span.start, format!("wrong number of arguments for {word}"));
        }
        let mut directory = None;
        let mut operands = values;
        if uses_directory && let Some((first, rest)) = values.split_first() {
            let text = self.text(first.span);
            let mut found = None;
            if let Some(key) = text.strip_prefix("dir_") {
                found = self.settings.dir(key);
            }
            let Some(asset) = found else {
                return Self::fail(first.span.start, "expected a configured dir_ name");
            };
            directory = Some(asset.path.clone());
            operands = rest;
        }
        let mut arguments = Vec::new();
        for (index, value) in operands.iter().enumerate() {
            let Some(kind) = parameters.get(index) else {
                break;
            };
            arguments.push(self.value_of_kind(value, *kind, word)?);
        }
        let visible = self.variables.len();
        let slots = self.take_results(&format!("'{word}'"), "gives", kinds, results)?;
        let checked = self.block(block)?;
        self.check_variables_used(visible)?;
        self.variables.truncate(visible);
        Ok(Statement::Given(program::Given {
            builtin,
            directory,
            arguments,
            slots,
            block: checked,
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
        if let Kind::Optional(inner) = kind
            && result_kind == *inner
        {
            let called = self.text(call.name.0);
            return Self::fail(
                name.0.start,
                format!(
                    "'{}' is {}, but '{called}' {gives} {}, so take it first, as in val found is {called} ... vit ret 0_{}, then set {} {} found",
                    self.text(name.0),
                    kind.name(),
                    result_kind.name(),
                    result_kind.name(),
                    self.text(name.0),
                    Conversion::ToOptional(*inner).word()
                ),
            );
        }
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
        for (position, kind) in results.iter().enumerate() {
            let Some(value) = values.get(position) else {
                return Self::fail(
                    span.start,
                    format!("'{name}' returns {shown}, but this return has {position}"),
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
                let (first, first_kind) = self.value(value)?;
                let mut flags = Vec::new();
                for (word, flag) in shift_or {
                    if first_kind != Kind::Flags {
                        return Self::fail(word.start, "expected a statement, got 'shift_or'");
                    }
                    flags.push(self.value_of_kind(flag, Kind::Flags, "shift_or")?);
                }
                let slot = self.allocate();
                let assign = Statement::Assign {
                    slot,
                    initial: first,
                    shift_or: flags,
                };
                (assign, first_kind, slot)
            }
            Initial::Conversion(conversion, values) => {
                let slot = self.allocate();
                let (converted, converted_kind) = self.conversion(slot, *conversion, values)?;
                (converted, converted_kind, slot)
            }
            Initial::Call(call) => {
                let (checked, result_kind, _) = self.call(call)?;
                let slot = self.allocate();
                let call_statement = Statement::Call {
                    slot,
                    call: checked,
                };
                (call_statement, result_kind, slot)
            }
        };
        self.declare(name, kind, slot, mutable);
        if let Statement::Assign {
            initial: Operand::Literal(Value::I32(value)),
            ..
        } = &statement
            && !mutable
            && let Some(variable) = self.variables.last_mut()
        {
            variable.known = Some(*value);
        }
        Ok(statement)
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
            let Some(first) = values.first() else {
                return self.expected(span.end, "a value");
            };
            if let Kind::Optional(inner) = kind
                && self.value(first)?.1 == *inner
                && !matches!(first.term, ast::Term::Literal(_))
            {
                return Self::fail(
                    first.span.start,
                    format!(
                        "'{shown}' is {}, but '{}' is {}, so convert it, as in set {shown} {} {}",
                        kind.name(),
                        self.text(first.span),
                        inner.name(),
                        Conversion::ToOptional(*inner).word(),
                        self.text(first.span)
                    ),
                );
            }
            let initial = self.value_of_kind(first, kind, "set ... tu")?;
            if let Some(extra) = values.get(1) {
                return Self::fail(
                    extra.span.start,
                    format!("set ... tu sets '{shown}' to a single value"),
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
                    "'{shown}' is {}, which cannot be used in arithmetic, but set ... tu sets it",
                    kind.name()
                ),
            );
        }
        // As += of C, set count tu_add 1_i32 2_i32 adds 1 and 2 to count, so
        // the operation starts from the variable, which is not repeated.
        let mut operands = vec![Operand::Slot(slot)];
        for value in values {
            if let ast::Term::Name(given) = value.term
                && given == name.1
            {
                return Self::fail(
                    value.span.start,
                    format!(
                        "set {shown} {word} starts from '{shown}', so '{shown}' is not repeated, as in set {shown} {word} 1_i32"
                    ),
                );
            }
            operands.push(self.value_of_kind(value, kind, word)?);
        }
        Ok(Statement::Calculate {
            slot,
            operation,
            values: operands,
            offset: span.start,
        })
    }

    // tu_opeto_i32 of an i32 variable, which a literal does not need, as
    // it is written 7_opeto_i32.
    fn optional_conversion(
        &mut self,
        slot: Slot,
        (span, inner): (Span, Kind),
        first: &ast::Operand<'input>,
        extra: Option<&ast::Operand<'input>>,
    ) -> Result<(Statement, Kind), Diagnostic> {
        let conversion = Conversion::ToOptional(inner);
        let word = conversion.word();
        let shown = self.text(first.span);
        if let ast::Term::Literal(_) = first.term
            && let Some(present) = super::value::present_literal(shown)
        {
            return Self::fail(
                first.span.start,
                format!("a literal needs no {word}, write {present}"),
            );
        }
        let left = self.value_of_kind(first, inner, word)?;
        if let Some(more) = extra {
            return self.expected_at(more.span, "a statement");
        }
        let statement = Statement::Convert {
            slot,
            conversion,
            left,
            right: None,
            offset: span.start,
        };
        Ok((statement, conversion.result_kind(inner)))
    }

    fn conversion(
        &mut self,
        slot: Slot,
        (span, conversion): (Span, Conversion),
        values: &[ast::Operand<'input>],
    ) -> Result<(Statement, Kind), Diagnostic> {
        let word = conversion.word();
        let Some(first) = values.first() else {
            return self.expected(span.end, "a value");
        };
        if let Conversion::ToOptional(inner) = conversion {
            return self.optional_conversion(slot, (span, inner), first, values.get(1));
        }
        // tu_i32 is how an aka_i32 becomes an i32.
        let (left, source) = match self.aka_parameter(first) {
            Some((operand, _)) if conversion == Conversion::ToI32 => (operand, Kind::I32),
            Some(_) | None => self.value(first)?,
        };
        let accepts_integer = conversion.accepts_integer();
        if !(source.is_real() || (accepts_integer && source.is_integer())) {
            let needed = if accepts_integer {
                "a number"
            } else {
                "f32 or f64"
            };
            return Self::fail(
                first.span.start,
                format!("{word} needs {needed}, got {}", source.name()),
            );
        }
        let mut right = None;
        let mut taken = 1;
        if conversion.is_binary() {
            let Some(second) = values.get(1) else {
                return self.expected(first.span.end, "a value");
            };
            right = Some(self.value_of_kind(second, source, word)?);
            taken = 2;
        }
        if let Some(extra) = values.get(taken) {
            return self.expected_at(extra.span, "a statement");
        }
        let statement = Statement::Convert {
            slot,
            conversion,
            left,
            right,
            offset: span.start,
        };
        Ok((statement, conversion.result_kind(source)))
    }
}
