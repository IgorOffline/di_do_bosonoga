use super::{Callable, Checker, Columns, Parameter, names_shown};
use crate::Diagnostic;
use crate::ast::{self, Arguments, Named, Span};
use crate::program::{self, AkaCheck, Callee, Operand, Slot, Statement};
use crate::value::Kind;

struct Target<'input> {
    callable: Callable,
    parameters: Vec<Parameter>,
    results: Vec<Kind>,
    // The names of the columns of an array, or of the values of a function.
    columns: Vec<&'input str>,
    gives: &'static str,
}

impl<'input> Target<'input> {
    fn columns(&self) -> Columns<'input> {
        Columns {
            names: self.columns.clone(),
            kinds: self.results.clone(),
        }
    }

    // How a call of the target is written, as in double_fn of ... or rows at 0_i32.
    fn example(&self, shown: &str) -> String {
        match self.callable {
            Callable::Array(_) | Callable::VarArray(_) => format!("{shown} at 0_i32"),
            Callable::Function(_) if self.parameters.is_empty() => shown.to_owned(),
            Callable::Function(_) => format!("{shown} of ..."),
        }
    }

    fn first(&self) -> &str {
        self.columns.first().copied().unwrap_or("count_i32")
    }

    fn needs_call_message(&self, shown: &str) -> String {
        let example = self.example(shown);
        let first = self.first();
        format!(
            "'{shown}' {}, which call ... with ret takes for its block, as in call {example} with ret {first} di ... do, or val takes one of, as in val value is {example} with ret {first}",
            gives_shown(self.gives, &self.columns())
        )
    }

    fn needs_val_message(&self, shown: &str) -> String {
        format!(
            "'{shown}' {}, which val, var or set ... to takes, as in val value is {} with ret {}",
            gives_shown(self.gives, &self.columns()),
            self.example(shown),
            self.first()
        )
    }
}

// What gives the values, as in returns quotient_i32 remainder_i32, or has
// the columns count_i32 marked_bool.
fn gives_shown(gives: &str, columns: &Columns<'_>) -> String {
    format!("{gives} {}", columns.shown())
}

// Where the arguments of a call end, or its name if it has none.
fn arguments_end(name: Named<'_>, arguments: &Arguments<'_>) -> usize {
    match arguments {
        Arguments::None => name.0.end,
        Arguments::Of(of, values) => match values.last() {
            Some(last) => last.span.end,
            None => of.end,
        },
        Arguments::At(_, index) => index.span.end,
    }
}

impl<'input> Checker<'_, 'input> {
    fn use_function(&mut self, index: usize) {
        if self.function == Some(index) {
            return;
        }
        if let Some(function) = self.functions.get_mut(index) {
            function.declaration.used = true;
        }
    }

    fn call_target(&mut self, span: Span, name: &'input str) -> Result<Target<'input>, Diagnostic> {
        if let Some(index) = self.variable(name) {
            let variable = &mut self.variables[index];
            if let Some((array, columns)) = variable.array.clone() {
                variable.declaration.used = true;
                return Ok(Target {
                    callable: Callable::VarArray(array),
                    parameters: vec![Parameter::plain(Kind::I32)],
                    results: columns.kinds,
                    columns: columns.names,
                    gives: "has the columns",
                });
            }
        }
        let Some(index) = self.function(name) else {
            if self.variable(name).is_some() {
                return self.expected(span.end, "a statement");
            }
            return self.fail_unknown_variable(span, name);
        };
        self.use_function(index);
        let (draws, input, called) = {
            let function = &self.functions[index];
            (function.draws, function.input, function.name)
        };
        if let Some(space) = draws {
            self.need_frame(span.start, space, &super::draw::Need::Call(called))?;
        }
        if input {
            self.need_input(
                span.start,
                format!(
                    "'{called}' reads gfx_key_held, so it can only be called in gfx_frame, gfx_key_down or gfx_key_up, or in a def that they call"
                ),
            )?;
        }
        let function = &self.functions[index];
        Ok(Target {
            callable: function.callable,
            parameters: function.parameters.clone(),
            results: function.results.clone(),
            columns: function.columns.clone(),
            gives: function.gives(),
        })
    }

    // The callee and its checked arguments, of a call of either form.
    fn callee(
        &mut self,
        name: Named<'input>,
        arguments: &Arguments<'input>,
    ) -> Result<(program::Call, Target<'input>), Diagnostic> {
        let target = self.call_target(name.0, name.1)?;
        let mut aka_checks = Vec::new();
        let (callee, values) = match target.callable {
            Callable::Array(array) => (Callee::Array(array), self.array_index(name, arguments)?),
            Callable::VarArray(array) => {
                (Callee::VarArray(array), self.array_index(name, arguments)?)
            }
            Callable::Function(code) => (
                Callee::Function(code),
                self.function_arguments(name, arguments, &target.parameters, &mut aka_checks)?,
            ),
        };
        let call = program::Call {
            callee,
            arguments: values,
            aka_checks,
            offset: name.0.start,
            column: 0,
        };
        Ok((call, target))
    }

    // A call that gives one value, which with ret names: one column of an
    // array, or one value of a function.
    pub(super) fn call(
        &mut self,
        call: &ast::Call<'input>,
    ) -> Result<(program::Call, Kind, &'static str), Diagnostic> {
        let (span, _) = call.name;
        let shown = self.text(span);
        let (mut checked, target) = self.callee(call.name, &call.arguments)?;
        if target.results.is_empty() {
            return Self::fail(
                span.start,
                format!(
                    "'{shown}' returns no values, so it is called with call, as in call {}",
                    target.example(shown)
                ),
            );
        }
        let columns = target.columns();
        let gives = gives_shown(target.gives, &columns);
        let ast::Column::Named((taken, column)) = call.taken else {
            let text = self.text(call.taken.span());
            return Self::fail(
                call.taken.span().start,
                format!(
                    "'{shown}' {gives}, which with ret names, as in {} with ret {}, not '{text}'",
                    target.example(shown),
                    target.first()
                ),
            );
        };
        let Some(position) = columns.position(column) else {
            return Self::fail(
                taken.start,
                format!("'{shown}' {gives}, so it has no {column}"),
            );
        };
        checked.column = position;
        Ok((checked, columns.kinds[position], "gives"))
    }

    // A call statement, of a function of no values, or one that takes the
    // values of a function or an array row of more than one for its block.
    pub(super) fn call_statement(
        &mut self,
        span: Span,
        name: Named<'input>,
        arguments: &Arguments<'input>,
        taken: Option<&ast::Taking<'input>>,
    ) -> Result<Statement, Diagnostic> {
        let shown = self.text(name.0);
        let (call, target) = self.callee(name, arguments)?;
        let Some(taking) = taken else {
            if target.results.is_empty() {
                return Ok(Statement::Invoke(program::Invoke {
                    call,
                    slots: Vec::new(),
                    block: Vec::new(),
                }));
            }
            if target.results.len() == 1 {
                return Self::fail(span.start, target.needs_val_message(shown));
            }
            let (at, _) = self.next_token(arguments_end(name, arguments));
            return Self::fail(at, target.needs_call_message(shown));
        };
        if target.results.is_empty() {
            return Self::fail(
                taking.with.start,
                format!(
                    "'{shown}' returns no values, so its call has neither with ret nor a block"
                ),
            );
        }
        if target.results.len() == 1 {
            return Self::fail(span.start, target.needs_val_message(shown));
        }
        let visible = self.variables.len();
        let slots = self.take_results(
            &format!("'{shown}'"),
            target.gives,
            &target.columns(),
            &taking.results,
        )?;
        let block = self.block(&taking.block)?;
        self.check_variables_used(visible)?;
        self.variables.truncate(visible);
        Ok(Statement::Invoke(program::Invoke { call, slots, block }))
    }

    // Declares the values that with ret takes, of a function, a given
    // operation or an array row, each once, in any order, as a val of its own
    // name or of the name after as, and gives their (index, slot) pairs.
    pub(super) fn take_results(
        &mut self,
        shown: &str,
        gives: &str,
        columns: &Columns<'input>,
        results: &[ast::Taken<'input>],
    ) -> Result<Vec<(usize, Slot)>, Diagnostic> {
        let mut slots: Vec<(usize, Slot)> = Vec::new();
        for taken in results {
            let ast::Column::Named((span, column)) = taken.column else {
                let at = taken.column.span();
                return Self::fail(
                    at.start,
                    format!(
                        "{shown} {}, which with ret names, as in with ret {}, not '{}'",
                        gives_shown(gives, columns),
                        columns.first(),
                        self.text(at)
                    ),
                );
            };
            let Some(position) = columns.position(column) else {
                return Self::fail(
                    span.start,
                    format!(
                        "{shown} {}, so it has no {column}",
                        gives_shown(gives, columns)
                    ),
                );
            };
            if slots.iter().any(|(before, _)| *before == position) {
                return Self::fail(span.start, format!("'{column}' is already taken"));
            }
            let binding = taken.binding.unwrap_or((span, column));
            if let Err(diagnostic) = self.new_name(binding) {
                if taken.binding.is_some() {
                    return Err(diagnostic);
                }
                let kind = columns.kinds[position];
                let base = column
                    .strip_suffix(&format!("_{}", kind.name()))
                    .unwrap_or("value");
                return Self::fail(
                    span.start,
                    format!(
                        "{}, so take it as another name, as in {column} as {}",
                        diagnostic.message,
                        super::aka::copy_name(base)
                    ),
                );
            }
            let slot = self.allocate();
            self.declare(binding, columns.kinds[position], slot, false);
            slots.push((position, slot));
        }
        Ok(slots)
    }

    fn array_index(
        &mut self,
        (span, _): Named<'input>,
        arguments: &Arguments<'input>,
    ) -> Result<Vec<Operand>, Diagnostic> {
        let shown = self.text(span);
        let how = format!(
            "'{shown}' is an array, which is read at one i32 index, as in {shown} at 0_i32"
        );
        match arguments {
            Arguments::At(_, operand) => {
                let context = format!("the index of '{shown}'");
                let index = self.value_of_kind(operand, Kind::I32, &context)?;
                Ok(vec![index])
            }
            Arguments::Of(of, _) => Self::fail(of.start, how),
            Arguments::None => {
                let (at, _) = self.next_token(span.end);
                Self::fail(at, how)
            }
        }
    }

    fn function_arguments(
        &mut self,
        (span, _): Named<'input>,
        arguments: &Arguments<'input>,
        parameters: &[Parameter],
        aka_checks: &mut Vec<AkaCheck>,
    ) -> Result<Vec<Operand>, Diagnostic> {
        let shown = self.text(span);
        let mut names = Vec::new();
        for parameter in parameters {
            names.push(match parameter.aka {
                Some(aka) => self.aka_name(aka),
                None => parameter.kind.name(),
            });
        }
        let taken = names_shown(&names);
        let values: &[ast::Operand<'input>] = match arguments {
            Arguments::At(at, _) => {
                return Self::fail(
                    at.start,
                    format!("'{shown}' is a function, and only an array is read at an index"),
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
                    format!("'{shown}' takes {taken}, which follow of, as in {shown} of ..."),
                );
            }
            Arguments::Of(_, values) => values,
            Arguments::None => &[],
        };
        let mut operands = Vec::new();
        for (index, parameter) in parameters.iter().enumerate() {
            let Some(value) = values.get(index) else {
                let mut after = span.end;
                if let Some(last) = values.last() {
                    after = last.span.end;
                }
                let (at, _) = self.next_token(after);
                return Self::fail(at, format!("'{shown}' takes {taken}, but is given {index}"));
            };
            let Some(aka) = parameter.aka else {
                operands.push(self.value_of_kind(value, parameter.kind, shown)?);
                continue;
            };
            let (operand, checked) = self.aka_argument(value, aka, shown)?;
            if !checked {
                aka_checks.push(AkaCheck {
                    argument: index,
                    aka,
                    offset: value.span.start,
                });
            }
            operands.push(operand);
        }
        if let Some(extra) = values.get(parameters.len()) {
            return Self::fail(
                extra.span.start,
                format!("'{shown}' takes {taken}, but is given more"),
            );
        }
        Ok(operands)
    }
}
