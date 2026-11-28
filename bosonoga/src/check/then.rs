use super::Checker;
use crate::Diagnostic;
use crate::ast::{self, Initial, Named, Span, Term};
use crate::program::{Comparison, Operand, Slot, Statement};
use crate::value::{Compare, Conversion, Kind, Operation, Value};

const KNOWN: &str = "a constant is known before the program runs, so it is computed from literals, earlier constants and settings";

// A step of then, checked, without the value so far that it starts from.
enum Step {
    Calculate(Operation, Vec<Operand>, usize),
    Convert(Conversion, Option<Operand>, usize),
    Join(Vec<Operand>),
}

impl Step {
    // The step from the value in from, written into into.
    fn statement(self, from: Slot, into: Slot) -> Statement {
        match self {
            Step::Calculate(operation, values, offset) => {
                let mut operands = vec![Operand::Slot(from)];
                operands.extend(values);
                Statement::Calculate {
                    slot: into,
                    operation,
                    values: operands,
                    offset,
                }
            }
            Step::Convert(conversion, right, offset) => Statement::Convert {
                slot: into,
                conversion,
                left: Operand::Slot(from),
                right,
                offset,
            },
            Step::Join(values) => {
                let mut operands = vec![Operand::Slot(from)];
                operands.extend(values);
                Statement::Join(into, operands)
            }
        }
    }

    // The step from a value that is known, for a constant, whose values are
    // all literals.
    fn evaluate(self, value: &Value, at: usize) -> Result<Value, Diagnostic> {
        let result = match self {
            Step::Calculate(operation, values, offset) => {
                let mut operands = vec![value];
                for operand in &values {
                    operands.push(literal(operand, offset)?);
                }
                operation
                    .evaluate(&operands)
                    .map_err(|message| (offset, message))
            }
            Step::Convert(conversion, right, offset) => {
                let mut other = None;
                if let Some(operand) = &right {
                    other = Some(literal(operand, offset)?);
                }
                conversion
                    .evaluate(value, other)
                    .map_err(|message| (offset, message))
            }
            Step::Join(values) => {
                let mut joined = value.to_string();
                for operand in &values {
                    joined.push_str(&literal(operand, at)?.to_string());
                }
                Ok(Value::Str(joined))
            }
        };
        result.map_err(|(offset, message)| Diagnostic::new(offset, message))
    }
}

fn literal(operand: &Operand, at: usize) -> Result<&Value, Diagnostic> {
    match operand {
        Operand::Literal(value) => Ok(value),
        Operand::Slot(_) | Operand::KeyHeld(_) => Err(Diagnostic::new(at, KNOWN)),
    }
}

impl<'input> Checker<'_, 'input> {
    // A value of a step, which a constant needs to be known.
    fn step_value(
        &self,
        operand: Operand,
        value: &ast::Operand<'input>,
        known: bool,
    ) -> Result<Operand, Diagnostic> {
        if known && !matches!(operand, Operand::Literal(_)) {
            return Self::fail(
                value.span.start,
                format!("{KNOWN}, not '{}'", self.text(value.span)),
            );
        }
        Ok(operand)
    }

    // The value that set sets is not repeated in its steps, which start from
    // the value so far.
    fn not_target(
        target: Option<&str>,
        word: &str,
        values: &[ast::Operand<'input>],
    ) -> Result<(), Diagnostic> {
        let Some(name) = target else {
            return Ok(());
        };
        for value in values {
            if let Term::Name(given) = value.term
                && given == name
            {
                return Self::fail(
                    value.span.start,
                    format!(
                        "then {word} starts from the value so far, so '{name}' is not repeated"
                    ),
                );
            }
        }
        Ok(())
    }

    // Checks a step from a value of kind, and gives the kind of its value.
    fn step(
        &mut self,
        target: Option<&str>,
        kind: Kind,
        then: &ast::Then<'input>,
        known: bool,
    ) -> Result<(Step, Kind), Diagnostic> {
        match then {
            ast::Then::Operation((span, operation), values) => {
                let word = self.text(*span);
                Self::not_target(target, word, values)?;
                if !kind.is_number() {
                    return Self::fail(
                        span.start,
                        format!(
                            "then {word} needs a number, but the value so far is {}",
                            kind.name()
                        ),
                    );
                }
                let mut operands = Vec::new();
                for value in values {
                    let operand = self.value_of_kind(value, kind, word)?;
                    operands.push(self.step_value(operand, value, known)?);
                }
                Ok((Step::Calculate(*operation, operands, span.start), kind))
            }
            ast::Then::Conversion((span, conversion), values) => {
                let word = conversion.word();
                Self::not_target(target, word, values)?;
                let (other, result) =
                    self.conversion_rest(*conversion, kind, (span.start, span.end), values)?;
                let right = match (other, values.first()) {
                    (Some(operand), Some(value)) => Some(self.step_value(operand, value, known)?),
                    (unchecked, _) => unchecked,
                };
                Ok((Step::Convert(*conversion, right, span.start), result))
            }
            ast::Then::Join(span, values) => {
                Self::not_target(target, "to_join", values)?;
                if kind != Kind::Str {
                    return Self::fail(
                        span.start,
                        format!(
                            "then to_join needs a str, but the value so far is {}",
                            kind.name()
                        ),
                    );
                }
                let mut operands = Vec::new();
                for value in values {
                    let operand = self.shown_value(value, "to_join")?;
                    operands.push(self.step_value(operand, value, known)?);
                }
                Ok((Step::Join(operands), Kind::Str))
            }
        }
    }

    // The steps of then from the value in start. A step that keeps the kind
    // writes the slot it read, one that gives the kind of home writes home,
    // and one that gives another kind a slot of its own. It gives the
    // statements, and the slot and kind of the last value.
    pub(super) fn then_steps(
        &mut self,
        target: Option<&str>,
        start: (Slot, Kind),
        home: (Slot, Kind),
        then: &[ast::Then<'input>],
    ) -> Result<(Vec<Statement>, Slot, Kind), Diagnostic> {
        let (mut slot, mut kind) = start;
        let mut statements = Vec::new();
        for step in then {
            let (checked, next) = self.step(target, kind, step, false)?;
            let into = if next == kind {
                slot
            } else if next == home.1 {
                home.0
            } else {
                self.allocate()
            };
            statements.push(checked.statement(slot, into));
            slot = into;
            kind = next;
        }
        Ok((statements, slot, kind))
    }

    // Writes a first value of set into a slot of its own when its kind is not
    // that of the variable, which the steps then bring back, and gives the
    // slot it is in.
    pub(super) fn retarget(
        &mut self,
        statement: &mut Statement,
        (slot, kind): (Slot, Kind),
        given: Kind,
    ) -> Slot {
        if given == kind {
            return slot;
        }
        let into = self.allocate();
        if let Statement::Convert { slot: written, .. } | Statement::Call { slot: written, .. } =
            statement
        {
            *written = into;
        }
        into
    }

    // A set and its steps, which end with a value of the kind of the variable.
    pub(super) fn set_then(
        &mut self,
        name: Named<'input>,
        target: (Slot, Kind),
        (first, slot, kind): (Statement, Slot, Kind),
        then: &[ast::Then<'input>],
    ) -> Result<Vec<Statement>, Diagnostic> {
        let mut statements = vec![first];
        if then.is_empty() {
            return Ok(statements);
        }
        let (steps, last, last_kind) = self.then_steps(Some(name.1), (slot, kind), target, then)?;
        statements.extend(steps);
        if last_kind != target.1 {
            return Self::fail(
                name.0.start,
                format!(
                    "'{}' is {}, but its steps end with {}",
                    self.text(name.0),
                    target.1.name(),
                    last_kind.name()
                ),
            );
        }
        if last != target.0 {
            statements.push(Statement::Assign {
                slot: target.0,
                initial: Operand::Slot(last),
                shift_or: Vec::new(),
            });
        }
        Ok(statements)
    }

    // The value of a constant, computed before the program runs.
    pub(super) fn known_value(
        &mut self,
        initial: &Initial<'input>,
        then: &[ast::Then<'input>],
    ) -> Result<Value, Diagnostic> {
        let mut value = match initial {
            Initial::Value { value, shift_or } => {
                let (checked, kind) = self.value(value)?;
                let mut first = literal(&self.step_value(checked, value, true)?, 0)?.clone();
                for (word, flag) in shift_or {
                    if kind != Kind::Flags {
                        return self.expected_at(*word, "an item");
                    }
                    let operand = self.value_of_kind(flag, Kind::Flags, "shift_or")?;
                    let more = self.step_value(operand, flag, true)?;
                    if let (Value::Flags(bits), Operand::Literal(Value::Flags(added))) =
                        (&mut first, more)
                    {
                        *bits |= added;
                    }
                }
                first
            }
            Initial::Conversion((span, conversion), values) => {
                let (left, right, _) = self.converted((*span, *conversion), values)?;
                let mut checked = Vec::new();
                for (operand, value) in [Some(left), right].into_iter().zip(values) {
                    if let Some(found) = operand {
                        checked.push(self.step_value(found, value, true)?);
                    }
                }
                let (Some(first), second) = (checked.first(), checked.get(1)) else {
                    return self.expected(span.end, "a value");
                };
                let mut other = None;
                if let Some(operand) = second {
                    other = Some(literal(operand, span.start)?);
                }
                match conversion.evaluate(literal(first, span.start)?, other) {
                    Ok(result) => result,
                    Err(message) => return Self::fail(span.start, message),
                }
            }
            Initial::Call(call) => {
                return Self::fail(
                    call.name.0.start,
                    format!("{KNOWN}, so it cannot call '{}'", self.text(call.name.0)),
                );
            }
        };
        for step in then {
            let at = match step {
                ast::Then::Operation((span, _), _)
                | ast::Then::Conversion((span, _), _)
                | ast::Then::Join(span, _) => span.start,
            };
            let (checked, _) = self.step(None, value.kind(), step, true)?;
            value = checked.evaluate(&value, at)?;
        }
        Ok(value)
    }

    // for name from first to end di ... do counts name, a val, from first to
    // the last before end, which are read once, before the first time.
    pub(super) fn for_statement(
        &mut self,
        span: Span,
        name: Named<'input>,
        (from, to): (&ast::Operand<'input>, &ast::Operand<'input>),
        block: &ast::Block<'input>,
    ) -> Result<Vec<Statement>, Diagnostic> {
        let (first, kind) = self.value(from)?;
        if !kind.is_integer() {
            return Self::fail(
                from.span.start,
                format!(
                    "for counts in i32, i64, u32 or u64, but '{}' is {}",
                    self.text(from.span),
                    kind.name()
                ),
            );
        }
        let end = self.value_of_kind(to, kind, "for ... from ... to")?;
        let mut statements = Vec::new();
        let last = match end {
            Operand::Literal(_) => end,
            Operand::Slot(_) | Operand::KeyHeld(_) => {
                let slot = self.allocate();
                statements.push(Statement::Assign {
                    slot,
                    initial: end,
                    shift_or: Vec::new(),
                });
                Operand::Slot(slot)
            }
        };
        let visible = self.variables.len();
        self.new_name(name)?;
        let slot = self.allocate();
        self.declare(name, kind, slot, false);
        statements.push(Statement::Assign {
            slot,
            initial: first,
            shift_or: Vec::new(),
        });
        let mut body = self.block(block)?;
        self.check_variables_used(visible)?;
        self.variables.truncate(visible);
        let one = Value::whole(kind, 1).unwrap_or(Value::I32(1));
        body.push(Statement::Calculate {
            slot,
            operation: Operation::Add,
            values: vec![Operand::Slot(slot), Operand::Literal(one)],
            offset: span.start,
        });
        statements.push(Statement::While {
            condition: vec![vec![Comparison {
                negate: false,
                left: Operand::Slot(slot),
                compare: Compare::Lt,
                right: last,
            }]],
            block: body,
            offset: span.start,
        });
        Ok(statements)
    }
}
