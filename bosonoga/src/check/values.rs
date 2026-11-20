use super::{Checker, SHIFT_BIT_MAX};
use crate::Diagnostic;
use crate::ast::{self, Section, Span, Term};
use crate::program::{Comparison, Condition, Operand};
use crate::value::{Compare, Kind, Value};

fn shift_bit(word: &str) -> Option<u32> {
    let digits = word.strip_prefix("shift_")?;
    let Ok(bit) = digits.parse::<u64>() else {
        return None;
    };
    if bit > SHIFT_BIT_MAX {
        return None;
    }
    u32::try_from(bit).ok()
}

impl<'input> Checker<'_, 'input> {
    pub(super) fn value_of_kind(
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

    pub(super) fn value(
        &mut self,
        operand: &ast::Operand<'input>,
    ) -> Result<(Operand, Kind), Diagnostic> {
        let at = operand.span.start;
        let text = self.text(operand.span);
        match &operand.term {
            Term::Literal(value) => Ok((Operand::Literal(value.clone()), value.kind())),
            Term::Shift(word) => match shift_bit(word) {
                Some(bit) => Ok((Operand::Literal(Value::Flags(1 << bit)), Kind::Flags)),
                None => Self::fail(
                    at,
                    format!("'{text}' is out of range, the last bit is shift_{SHIFT_BIT_MAX}"),
                ),
            },
            Term::Result(_) => Self::fail(
                at,
                format!(
                    "{text} is not a value, a call names the values that it takes, as in call ... with ret 0_i32 as count"
                ),
            ),
            Term::GfxValue(word) => self.gfx_value(at, word),
            Term::Name(name) => self.named_value(operand.span, name),
        }
    }

    fn gfx_value(&self, at: usize, word: &str) -> Result<(Operand, Kind), Diagnostic> {
        let in_function = self.function.is_some();
        if word == "gfx_key" {
            if let Some(slot) = self.program.key_slot
                && self.section == Some(Section::GfxKeyDown)
                && !in_function
            {
                return Ok((Operand::Slot(slot), Kind::InputKey));
            }
            return Self::fail(at, "gfx_key can only be in gfx_key_down");
        }
        if let Some(slot) = self.program.delta_time_slot
            && self.section == Some(Section::GfxFrame)
            && !in_function
        {
            return Ok((Operand::Slot(slot), Kind::F32));
        }
        Self::fail(at, "gfx_delta_time can only be in gfx_frame")
    }

    fn named_value(&mut self, span: Span, name: &str) -> Result<(Operand, Kind), Diagnostic> {
        if let Some(index) = self.constant(name) {
            let constant = &mut self.constants[index];
            constant.declaration.used = true;
            let value = constant.value.clone();
            let kind = value.kind();
            return Ok((Operand::Literal(value), kind));
        }
        if let Some(value) = self.setting(name) {
            let kind = value.kind();
            return Ok((Operand::Literal(value), kind));
        }
        let Some(index) = self.variable(name) else {
            return self.fail_unknown_variable(span, name);
        };
        let variable = &mut self.variables[index];
        if variable.array.is_some() {
            return self.fail_unknown_variable(span, name);
        }
        variable.declaration.used = true;
        Ok((Operand::Slot(variable.slot), variable.kind))
    }

    pub(super) fn condition(
        &mut self,
        condition: &ast::Condition<'input>,
    ) -> Result<Condition, Diagnostic> {
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
            Some((span, given, value)) => {
                let (checked, right_kind) = self.value(value)?;
                self.check_compare(*span, *given, left_kind, right_kind)?;
                (*given, checked)
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
        let at = span.start;
        if compare == Compare::ShiftHas {
            if left == Kind::Flags && right == Kind::Flags {
                return Ok(());
            }
            return Self::fail(
                at,
                format!(
                    "shift_has needs flags, not {} and {}",
                    left.name(),
                    right.name()
                ),
            );
        }
        if left != right {
            return Self::fail(
                at,
                format!("cannot compare {} with {}", left.name(), right.name()),
            );
        }
        if left == Kind::Flags {
            return Self::fail(at, "flags can only be compared with shift_has");
        }
        if left == Kind::Color {
            return Self::fail(at, "colors cannot be compared");
        }
        let is_equality = compare == Compare::Eq || compare == Compare::NotEq;
        let is_tolerance = matches!(
            compare,
            Compare::Eq3 | Compare::Eq6 | Compare::NotEq3 | Compare::NotEq6
        );
        let only_equality = matches!(
            left,
            Kind::Str | Kind::Bool | Kind::InputKey | Kind::Font | Kind::Icon
        );
        if only_equality && !is_equality {
            return Self::fail(
                at,
                format!("{} can only be compared with eq or not_eq", left.name()),
            );
        }
        if is_equality && left.is_real() {
            return Self::fail(
                at,
                format!(
                    "{} has no exact {word}, use {word}_3 or {word}_6",
                    left.name()
                ),
            );
        }
        if is_tolerance && left.is_integer() {
            return Self::fail(
                at,
                format!(
                    "{word} only works on f32 or f64, {} uses eq or not_eq",
                    left.name()
                ),
            );
        }
        Ok(())
    }
}
