use crate::ast::{self, Main, Span, Term};
use crate::lexer::{Lexer, Tok};
use crate::program::{Comparison, Condition, Operand, Program, Statement};
use crate::value::{Compare, Kind, Value};
use crate::{Diagnostic, shown};

const SHIFT_BIT_MAX: u64 = 31;

struct Variable<'input> {
    name: &'input str,
    span: Span,
    kind: Kind,
    slot: usize,
    used: bool,
    mutable: bool,
}

struct Checker<'input> {
    source: &'input str,
    variables: Vec<Variable<'input>>,
    slot_count: usize,
}

pub(crate) fn check(source: &str, main: &Main<'_>) -> Result<Program, Diagnostic> {
    let mut checker = Checker {
        source,
        variables: Vec::new(),
        slot_count: 0,
    };
    let statements = checker.block(&main.statements)?;
    Ok(Program {
        statements,
        slot_count: checker.slot_count,
        end: main.end,
    })
}

impl<'input> Checker<'input> {
    fn text(&self, span: Span) -> &'input str {
        shown(&self.source[span.start..span.end])
    }

    fn block(&mut self, block: &[ast::Statement<'input>]) -> Result<Vec<Statement>, Diagnostic> {
        let visible = self.variables.len();
        let mut statements = Vec::new();
        for statement in block {
            if let Some(checked) = self.statement(statement)? {
                statements.push(checked);
            }
        }
        if let Some(unused) = self.variables[visible..].iter().find(|v| !v.used) {
            return Err(Diagnostic::new(
                unused.span.start,
                format!("'{}' is never read", self.text(unused.span)),
            ));
        }
        self.variables.truncate(visible);
        Ok(statements)
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
            ast::Statement::Declare {
                name,
                mutable,
                initial,
                shift_or,
            } => self.declare(*name, *mutable, initial, shift_or)?,
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
            ast::Statement::Nihil => return Ok(None),
            ast::Statement::Return(status) => Statement::Return(*status),
        }))
    }

    fn declare(
        &mut self,
        (span, name): (Span, &'input str),
        mutable: bool,
        initial: &ast::Operand<'input>,
        shift_or: &[(Span, ast::Operand<'input>)],
    ) -> Result<Statement, Diagnostic> {
        if self.variables.iter().any(|v| v.name == name) {
            return Err(Diagnostic::new(
                span.start,
                format!("variable '{}' already exists", self.text(span)),
            ));
        }
        if name.len() > "_fn".len() && name.ends_with("_fn") {
            return Err(Diagnostic::new(
                span.start,
                format!(
                    "'{}' ends with _fn, which only the name of a function does",
                    self.text(span)
                ),
            ));
        }
        let (first, kind) = self.value(initial)?;
        let mut flags = Vec::new();
        for (word, value) in shift_or {
            if kind != Kind::Flags {
                return Err(Diagnostic::new(
                    word.start,
                    "expected a statement, got 'shift_or'",
                ));
            }
            let (operand, operand_kind) = self.value(value)?;
            if operand_kind != Kind::Flags {
                return Err(Diagnostic::new(
                    value.span.start,
                    format!(
                        "shift_or needs flags, but '{}' is {}",
                        self.text(value.span),
                        operand_kind.name()
                    ),
                ));
            }
            flags.push(operand);
        }
        let slot = self.slot_count;
        self.slot_count += 1;
        self.variables.push(Variable {
            name,
            span,
            kind,
            slot,
            used: false,
            mutable,
        });
        Ok(Statement::Assign {
            slot,
            initial: first,
            shift_or: flags,
        })
    }

    fn set(
        &mut self,
        (name_span, name): (Span, &'input str),
        (span, operation): (Span, crate::value::Operation),
        values: &[ast::Operand<'input>],
    ) -> Result<Statement, Diagnostic> {
        let variable = self
            .variables
            .iter()
            .find(|v| v.name == name)
            .ok_or_else(|| {
                Diagnostic::new(name_span.start, format!("unknown variable '{name}'"))
            })?;
        if !variable.mutable {
            return Err(Diagnostic::new(
                name_span.start,
                format!("'{name}' is a val and cannot be set"),
            ));
        }
        let (slot, kind) = (variable.slot, variable.kind);
        if !operation.accepts(kind, values.len()) {
            return Err(Diagnostic::new(
                span.start,
                format!(
                    "invalid operands for {} on {}",
                    self.text(span),
                    kind.name()
                ),
            ));
        }
        let mut operands = Vec::new();
        for value in values {
            let (operand, operand_kind) = self.value(value)?;
            if operand_kind != kind {
                return Err(Diagnostic::new(
                    value.span.start,
                    format!("expected {}, got {}", kind.name(), operand_kind.name()),
                ));
            }
            operands.push(operand);
        }
        Ok(Statement::Calculate {
            slot,
            operation,
            values: operands,
            offset: span.start,
        })
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
            Term::Result(_) => Err(Diagnostic::new(
                at,
                format!(
                    "{text} is not a value, a call names the values that it takes, as in call ... with ret 0_i32 as count"
                ),
            )),
            Term::GfxValue(word) => Err(Diagnostic::new(
                at,
                if *word == "gfx_key" {
                    "gfx_key can only be in gfx_key_down"
                } else {
                    "gfx_delta_time can only be in gfx_frame"
                },
            )),
            Term::Name(name) => {
                let Some(variable) = self.variables.iter_mut().rev().find(|v| v.name == *name)
                else {
                    let what = if name.len() > "_fn".len() && name.ends_with("_fn") {
                        "function"
                    } else {
                        "variable"
                    };
                    return Err(Diagnostic::new(at, format!("unknown {what} '{text}'")));
                };
                variable.used = true;
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
            None => return Err(self.expected_comparison(comparison.left.span.end)),
        };
        Ok(Comparison {
            negate: comparison.negate,
            left,
            compare,
            right,
        })
    }

    fn expected_comparison(&self, after: usize) -> Diagnostic {
        match Lexer::starting_at(self.source, after).next() {
            Some(Ok((start, token, end))) if token != Tok::End => Diagnostic::new(
                start,
                format!(
                    "expected a comparison, got '{}'",
                    shown(&self.source[start..end])
                ),
            ),
            Some(Ok((start, ..)) | Err(Diagnostic { offset: start, .. })) => {
                Diagnostic::new(start, "expected a comparison, got end of file")
            }
            None => Diagnostic::new(after, "expected a comparison, got end of file"),
        }
    }

    fn check_compare(
        &self,
        span: Span,
        compare: Compare,
        left: Kind,
        right: Kind,
    ) -> Result<(), Diagnostic> {
        let word = self.text(span);
        let fail = |message: String| Err(Diagnostic::new(span.start, message));
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
        if matches!(left, Kind::Str | Kind::Bool)
            && !matches!(compare, Compare::Eq | Compare::NotEq)
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
