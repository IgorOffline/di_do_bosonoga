use super::{Checker, values_shown};
use crate::Diagnostic;
use crate::ast::{self, ExitStatus, Initial, Named, Section, Span};
use crate::io::Builtin;
use crate::program::{self, Operand, Slot, Statement};
use crate::value::{Conversion, Kind, Operation};

impl<'input> Checker<'_, 'input> {
    pub(super) fn statement(
        &mut self,
        statement: &ast::Statement<'input>,
    ) -> Result<Option<Statement>, Diagnostic> {
        let checked = match statement {
            ast::Statement::Info(values) => Statement::Info(self.operands(values)?),
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
                Statement::Join(slot, self.operands(values)?)
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
        };
        Ok(Some(checked))
    }

    fn operands(&mut self, values: &[ast::Operand<'input>]) -> Result<Vec<Operand>, Diagnostic> {
        let mut operands = Vec::new();
        for value in values {
            let (operand, _) = self.value(value)?;
            operands.push(operand);
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
            let holds = self.condition(condition)?;
            let statements = self.block(block)?;
            checked.push((holds, statements));
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
        let mut operands = Vec::new();
        for value in values {
            operands.push(self.value_of_kind(value, kind, word)?);
        }
        if operands.len() == 1 {
            if operation == Operation::Modulo {
                let mut after = span.end;
                if let Some(first) = values.first() {
                    after = first.span.end;
                }
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
        slot: Slot,
        (span, conversion): (Span, Conversion),
        values: &[ast::Operand<'input>],
    ) -> Result<(Statement, Kind), Diagnostic> {
        let word = conversion.word();
        let Some(first) = values.first() else {
            return self.expected(span.end, "a value");
        };
        let (left, source) = self.value(first)?;
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
