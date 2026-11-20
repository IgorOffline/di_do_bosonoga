use super::{Callable, Checker, kinds_named, split_indexed, values_shown};
use crate::Diagnostic;
use crate::ast::{self, Arguments, Span};
use crate::program::{self, Callee, Operand};
use crate::value::Kind;

struct Target {
    callable: Callable,
    parameters: Vec<Kind>,
    results: Vec<Kind>,
    gives: &'static str,
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

    fn call_target(&mut self, span: Span, name: &'input str) -> Result<Target, Diagnostic> {
        if let Some(index) = self.variable(name) {
            let variable = &mut self.variables[index];
            if let Some((array, kinds)) = variable.array.clone() {
                variable.declaration.used = true;
                return Ok(Target {
                    callable: Callable::VarArray(array),
                    parameters: vec![Kind::I32],
                    results: kinds,
                    gives: "has rows of",
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
        let function = &self.functions[index];
        Ok(Target {
            callable: function.callable,
            parameters: function.parameters.clone(),
            results: function.results.clone(),
            gives: function.gives(),
        })
    }

    pub(super) fn call(
        &mut self,
        call: &ast::Call<'input>,
    ) -> Result<(program::Call, Kind, &'static str), Diagnostic> {
        let (span, name) = call.name;
        let shown = self.text(span);
        let target = self.call_target(span, name)?;
        let (callee, arguments) = match target.callable {
            Callable::Array(array) => (Callee::Array(array), self.array_index(call)?),
            Callable::VarArray(array) => (Callee::VarArray(array), self.array_index(call)?),
            Callable::Function(code) => (
                Callee::Function(code),
                self.function_arguments(call, &target.parameters)?,
            ),
        };
        let results = &target.results;
        let gives = target.gives;
        if results.is_empty() {
            return Self::fail(
                span.start,
                format!("'{shown}' returns no values, so it is called with call"),
            );
        }
        if results.len() > 1 {
            return Self::fail(
                span.start,
                format!(
                    "'{shown}' {gives} {}, which call ... with ret takes for its block, each named, as in with ret 0_{} as value",
                    kinds_named(results),
                    results[0].name()
                ),
            );
        }
        let result = results[0];
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
        let checked = program::Call {
            callee,
            arguments,
            offset: span.start,
        };
        Ok((checked, result, gives))
    }

    fn array_index(&mut self, call: &ast::Call<'input>) -> Result<Vec<Operand>, Diagnostic> {
        let (span, _) = call.name;
        let shown = self.text(span);
        let how = format!(
            "'{shown}' is an array, which is read at one i32 index, as in {shown} at 0_i32"
        );
        match &call.arguments {
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
        call: &ast::Call<'input>,
        parameters: &[Kind],
    ) -> Result<Vec<Operand>, Diagnostic> {
        let (span, _) = call.name;
        let shown = self.text(span);
        let taken = values_shown(parameters);
        let values: &[ast::Operand<'input>] = match &call.arguments {
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
        for (index, kind) in parameters.iter().enumerate() {
            let Some(value) = values.get(index) else {
                let mut after = span.end;
                if let Some(last) = values.last() {
                    after = last.span.end;
                }
                let (at, _) = self.next_token(after);
                return Self::fail(at, format!("'{shown}' takes {taken}, but is given {index}"));
            };
            operands.push(self.value_of_kind(value, *kind, shown)?);
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
