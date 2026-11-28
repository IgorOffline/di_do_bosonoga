use super::Checker;
use crate::Diagnostic;
use crate::ast::{self, ExitStatus, Initial, Named, Section, Span};
use crate::io::Builtin;
use crate::program::{self, Operand, Slot, Statement};
use crate::value::{Compare, Conversion, Kind, Operation, Value};

impl<'input> Checker<'_, 'input> {
    // The statements that a statement runs as: none for pass, and more than
    // one for then and for.
    pub(super) fn statement(
        &mut self,
        statement: &ast::Statement<'input>,
    ) -> Result<Vec<Statement>, Diagnostic> {
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
                    return Self::fail(name.0.start, "to_join needs a str variable");
                }
                Statement::Join(slot, self.operands(values, "to_join")?)
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
                then,
            } => return self.set(*name, *operation, values, then),
            ast::Statement::SetConversion {
                name,
                conversion,
                values,
                then,
            } => return self.set_conversion(*name, *conversion, values, then),
            ast::Statement::SetCall {
                name,
                operation,
                call,
                then,
            } => return self.set_call(*name, *operation, call, then),
            ast::Statement::Declare {
                name,
                mutable,
                initial,
                then,
            } => return self.declaration_statement(*name, *mutable, initial, then),
            ast::Statement::DeclareArray {
                name,
                mutable,
                array,
                columns,
                rows,
            } => self.var_array(*name, *mutable, *array, columns, rows)?,
            ast::Statement::SetAt {
                name,
                index,
                column,
                operation,
                values,
            } => return self.set_at(*name, index, *column, *operation, values),
            ast::Statement::If {
                branches,
                otherwise,
            } => self.if_statement(branches, otherwise.as_ref())?,
            ast::Statement::While(span, condition, block) => {
                self.while_statement(*span, condition, block)?
            }
            ast::Statement::For {
                span,
                name,
                from,
                to,
                block,
            } => return self.for_statement(*span, *name, (from, to), block),
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
            ast::Statement::Pass => return Ok(Vec::new()),
            ast::Statement::Return(span, status) => self.return_status(*span, *status)?,
            ast::Statement::ReturnValues(span, values) => self.return_values(*span, values)?,
        };
        Ok(vec![checked])
    }

    fn while_statement(
        &mut self,
        span: Span,
        condition: &ast::Condition<'input>,
        block: &ast::Block<'input>,
    ) -> Result<Statement, Diagnostic> {
        let holds = self.condition(condition)?;
        Ok(Statement::While {
            condition: holds,
            block: self.block(block)?,
            offset: span.start,
        })
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
        results: &[ast::Taken<'input>],
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
        let columns = super::Columns {
            names: builtin.result_names().to_vec(),
            kinds: kinds.to_vec(),
        };
        let slots = self.take_results(&format!("'{word}'"), "gives", &columns, results)?;
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
        then: &[ast::Then<'input>],
    ) -> Result<Vec<Statement>, Diagnostic> {
        let (slot, kind) = self.target(name)?;
        let (mut converted, converted_kind) = self.conversion(slot, conversion, values)?;
        if !then.is_empty() {
            let from = self.retarget(&mut converted, (slot, kind), converted_kind);
            return self.set_then(name, (slot, kind), (converted, from, converted_kind), then);
        }
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
        Ok(vec![converted])
    }

    fn set_call(
        &mut self,
        name: Named<'input>,
        (_, operation): (Span, Operation),
        call: &ast::Call<'input>,
        then: &[ast::Then<'input>],
    ) -> Result<Vec<Statement>, Diagnostic> {
        let (slot, kind) = self.target(name)?;
        if operation != Operation::Copy {
            return self.fail_unknown_variable(call.name.0, call.name.1);
        }
        let (checked, result_kind, gives) = self.call(call)?;
        if !then.is_empty() {
            let mut called = Statement::Call {
                slot,
                call: checked,
            };
            let from = self.retarget(&mut called, (slot, kind), result_kind);
            return self.set_then(name, (slot, kind), (called, from, result_kind), then);
        }
        if let Kind::Optional(inner) = kind
            && result_kind == *inner
        {
            let called = self.text(call.name.0);
            return Self::fail(
                name.0.start,
                format!(
                    "'{}' is {}, but '{called}' {gives} {}, so take it first, as in val found is {called} ... with ret {}, then set {} {} found",
                    self.text(name.0),
                    kind.name(),
                    result_kind.name(),
                    self.text(call.taken.span()),
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
        Ok(vec![Statement::Call {
            slot,
            call: checked,
        }])
    }

    fn return_status(&self, span: Span, status: ExitStatus) -> Result<Statement, Diagnostic> {
        if self.function.is_some() {
            let (at, _) = self.next_token(span.end);
            return Self::fail(
                at,
                "only main and gfx_main return an exit status, a function returns its values",
            );
        }
        if !self.in_main() {
            return Self::fail(span.start, "only main and gfx_main can return");
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
            return Self::fail(span.start, "only main and gfx_main can return");
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
        let shown = function.columns.join(" ");
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
        then: &[ast::Then<'input>],
    ) -> Result<Vec<Statement>, Diagnostic> {
        self.new_name(name)?;
        let (statement, kind, slot) = self.initial(initial)?;
        let (steps, last, last_kind) = self.then_steps(None, (slot, kind), (slot, kind), then)?;
        self.declare(name, last_kind, last, mutable);
        if let Statement::Assign {
            initial: Operand::Literal(Value::I32(value)),
            ..
        } = &statement
            && !mutable
            && then.is_empty()
            && let Some(variable) = self.variables.last_mut()
        {
            variable.known = Some(*value);
        }
        let mut statements = vec![statement];
        statements.extend(steps);
        Ok(statements)
    }

    // The first value of a val or var, and the slot it is in.
    fn initial(
        &mut self,
        initial: &Initial<'input>,
    ) -> Result<(Statement, Kind, Slot), Diagnostic> {
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
        Ok((statement, kind, slot))
    }

    fn set(
        &mut self,
        name: Named<'input>,
        operation: (Span, Operation),
        values: &[ast::Operand<'input>],
        then: &[ast::Then<'input>],
    ) -> Result<Vec<Statement>, Diagnostic> {
        let (slot, kind) = self.target(name)?;
        // set ... to with steps starts from a value of any kind, in a slot of
        // its own, as long as the steps end with the kind of the variable.
        if operation.1 == Operation::Copy
            && !then.is_empty()
            && let [first] = values
        {
            let (initial, given) = self.value(first)?;
            let from = if given == kind { slot } else { self.allocate() };
            let assign = Statement::Assign {
                slot: from,
                initial,
                shift_or: Vec::new(),
            };
            return self.set_then(name, (slot, kind), (assign, from, given), then);
        }
        let first = self.set_first(name, (slot, kind), operation, values)?;
        self.set_then(name, (slot, kind), (first, slot, kind), then)
    }

    pub(super) fn set_first(
        &mut self,
        name: Named<'input>,
        (slot, kind): (Slot, Kind),
        (span, operation): (Span, Operation),
        values: &[ast::Operand<'input>],
    ) -> Result<Statement, Diagnostic> {
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
            let initial = self.value_of_kind(first, kind, "set ... to")?;
            if let Some(extra) = values.get(1) {
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
        // As += of C, set count to_add 1_i32 2_i32 adds 1 and 2 to count, so
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

    fn conversion(
        &mut self,
        slot: Slot,
        (span, conversion): (Span, Conversion),
        values: &[ast::Operand<'input>],
    ) -> Result<(Statement, Kind), Diagnostic> {
        let (left, right, kind) = self.converted((span, conversion), values)?;
        let statement = Statement::Convert {
            slot,
            conversion,
            left,
            right,
            offset: span.start,
        };
        Ok((statement, kind))
    }

    // The values of a conversion and the kind it gives.
    pub(super) fn converted(
        &mut self,
        (span, conversion): (Span, Conversion),
        values: &[ast::Operand<'input>],
    ) -> Result<(Operand, Option<Operand>, Kind), Diagnostic> {
        let word = conversion.word();
        let Some(first) = values.first() else {
            return self.expected(span.end, "a value");
        };
        // to_optional_i32 of an i32 variable, which a literal does not need,
        // as it is written 7_optional_i32.
        if let Conversion::ToOptional(inner) = conversion {
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
            if let Some(more) = values.get(1) {
                return self.expected_at(more.span, "a statement");
            }
            return Ok((left, None, conversion.result_kind(inner)));
        }
        // to_i32 is how an aka_i32 becomes an i32.
        let (left, source) = match self.aka_parameter(first) {
            Some((operand, _)) if conversion == Conversion::ToI32 => (operand, Kind::I32),
            Some(_) | None => self.value(first)?,
        };
        let rest = values.get(1..).unwrap_or_default();
        let (right, kind) =
            self.conversion_rest(conversion, source, (first.span.start, first.span.end), rest)?;
        Ok((left, right, kind))
    }

    // Checks a conversion of a value of source, which starts at the first
    // offset and ends at the second, and gives its other value, if it has
    // one, of rest, and the kind it gives.
    pub(super) fn conversion_rest(
        &mut self,
        conversion: Conversion,
        source: Kind,
        (at, after): (usize, usize),
        rest: &[ast::Operand<'input>],
    ) -> Result<(Option<Operand>, Kind), Diagnostic> {
        let word = conversion.word();
        if let Conversion::ToOptional(inner) = conversion {
            if source != inner {
                return Self::fail(
                    at,
                    format!("{word} needs {}, got {}", inner.name(), source.name()),
                );
            }
            if let Some(more) = rest.first() {
                return self.expected_at(more.span, "a statement");
            }
            return Ok((None, conversion.result_kind(inner)));
        }
        let accepts_integer = conversion.accepts_integer();
        if !(source.is_real() || (accepts_integer && source.is_integer())) {
            let needed = if accepts_integer {
                "a number"
            } else {
                "f32 or f64"
            };
            return Self::fail(at, format!("{word} needs {needed}, got {}", source.name()));
        }
        let mut right = None;
        let mut taken = 0;
        if conversion.is_binary() {
            let Some(second) = rest.first() else {
                return self.expected(after, "a value");
            };
            right = Some(self.value_of_kind(second, source, word)?);
            taken = 1;
        }
        if let Some(extra) = rest.get(taken) {
            return self.expected_at(extra.span, "a statement");
        }
        Ok((right, conversion.result_kind(source)))
    }
}
