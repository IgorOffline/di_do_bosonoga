use super::{Checker, SHIFT_BIT_MAX};
use crate::Diagnostic;
use crate::ast::{self, Section, Span, Term};
use crate::program::{Comparison, Condition, Operand, Program, Slot};
use crate::value::{Compare, Kind, Value};

// The present literal of a literal, as 7_optional_i32 of 7_i32.
pub(super) fn present_literal(text: &str) -> Option<String> {
    let (value, kind) = text.rsplit_once('_')?;
    Some(format!("{value}_optional_{kind}"))
}

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
        if let Kind::Optional(inner) = kind
            && actual == *inner
        {
            let shown = self.text(operand.span);
            return Self::fail(
                operand.span.start,
                format!(
                    "{context} needs {}, but '{shown}' is {}, {}",
                    kind.name(),
                    actual.name(),
                    self.present_hint(operand, kind)
                ),
            );
        }
        if actual != kind {
            let shown = self.text(operand.span);
            // nihil_i32 says i32, but is an optional_i32 that is absent.
            let mut nihil = String::new();
            if let Term::Literal(Value::Optional { value: None, .. }) = operand.term {
                nihil = format!(", as {shown} is an absent {}", actual.name());
            }
            return Self::fail(
                operand.span.start,
                format!(
                    "{context} needs {}, but '{shown}' is {}{nihil}",
                    kind.name(),
                    actual.name()
                ),
            );
        }
        Ok(checked)
    }

    // How a value of the kind inside an optional becomes one: a literal is
    // written as a present literal, and a variable is converted.
    pub(super) fn present_hint(&self, operand: &ast::Operand<'input>, kind: Kind) -> String {
        let shown = self.text(operand.span);
        if let Term::Literal(_) = operand.term
            && let Some(present) = present_literal(shown)
        {
            return format!("so write {present}");
        }
        format!(
            "so convert it first, as in val present is to_{} {shown}",
            kind.name()
        )
    }

    // A value that info, to_join or gfx_draw_text shows, which an optional is not.
    pub(super) fn shown_value(
        &mut self,
        operand: &ast::Operand<'input>,
        word: &str,
    ) -> Result<Operand, Diagnostic> {
        let (checked, kind) = self.value(operand)?;
        if kind.is_optional() {
            let shown = self.text(operand.span);
            return Self::fail(
                operand.span.start,
                format!(
                    "'{shown}' is {}, which {word} cannot show, but its value can, as in if some {shown} as value",
                    kind.name()
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

    fn gfx_value(&mut self, at: usize, word: &str) -> Result<(Operand, Kind), Diagnostic> {
        let in_function = self.function.is_some();
        if word == "gfx_key" {
            if let Some(slot) = self.program.key_slot
                && matches!(self.section, Some(Section::GfxKeyDown | Section::GfxKeyUp))
                && !in_function
            {
                return Ok((Operand::Slot(slot), Kind::InputKey));
            }
            return Self::fail(at, "gfx_key can only be in gfx_key_down or gfx_key_up");
        }
        if word == "gfx_mouse_captured" {
            if !self.in_mouse_section() {
                return Self::fail(
                    at,
                    "gfx_mouse_captured can only be in gfx_frame, gfx_key_down or gfx_key_up",
                );
            }
            let slot = self.read_slot(|program| &mut program.mouse_captured_slot);
            return Ok((Operand::Slot(slot), Kind::Bool));
        }
        let in_frame = self.section == Some(Section::GfxFrame) && !in_function;
        if word == "gfx_mouse_move_x" || word == "gfx_mouse_move_y" {
            if !in_frame {
                return Self::fail(at, format!("{word} can only be in gfx_frame"));
            }
            let slot = if word == "gfx_mouse_move_x" {
                self.read_slot(|program| &mut program.mouse_move_x_slot)
            } else {
                self.read_slot(|program| &mut program.mouse_move_y_slot)
            };
            return Ok((Operand::Slot(slot), Kind::F32));
        }
        if word == "gfx_heap_bytes" {
            if !in_frame {
                return Self::fail(at, "gfx_heap_bytes can only be in gfx_frame");
            }
            // Its slot is made by the first read, so that a program that does
            // not read it does not measure the heap every frame.
            let slot = self.read_slot(|program| &mut program.heap_slot);
            return Ok((Operand::Slot(slot), Kind::U64));
        }
        if let Some(slot) = self.program.delta_time_slot
            && in_frame
        {
            return Ok((Operand::Slot(slot), Kind::F32));
        }
        Self::fail(at, "gfx_delta_time can only be in gfx_frame")
    }

    // The slot of a gfx value that the window sets, made by its first read.
    fn read_slot(&mut self, slot_of: fn(&mut Program) -> &mut Option<Slot>) -> Slot {
        if let Some(slot) = *slot_of(&mut self.program) {
            return slot;
        }
        let slot = self.allocate();
        *slot_of(&mut self.program) = Some(slot);
        slot
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
        self.check_not_aka(span, index)?;
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
        if let Some(presence) = &comparison.presence {
            if presence.binding.is_some() {
                return Self::fail(
                    presence.span.start,
                    "some ... as ... names the value for the block of an if or else if, so it is the whole condition, without not, and or or",
                );
            }
            let (left, inner) = self.optional_value(presence.span, &comparison.left)?;
            return Ok(Comparison {
                negate: comparison.negate,
                left,
                compare: Compare::NotEq,
                right: Operand::Literal(Value::Optional { inner, value: None }),
            });
        }
        let (left, left_kind) = self.value(&comparison.left)?;
        if left_kind.is_optional() && comparison.right.is_none() {
            let shown = self.text(comparison.left.span);
            return Self::fail(
                comparison.left.span.start,
                format!(
                    "'{shown}' is {}, which is tested with some, as in if some {shown} or if some {shown} as value",
                    left_kind.name()
                ),
            );
        }
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
        if left.is_optional() || right.is_optional() {
            let optional = if left.is_optional() { left } else { right };
            return Self::fail(
                at,
                format!(
                    "{} cannot be compared, but its value can, as in if some count as value",
                    optional.name()
                ),
            );
        }
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

    // The optional that some tests, and the kind of its value.
    pub(super) fn optional_value(
        &mut self,
        some: Span,
        operand: &ast::Operand<'input>,
    ) -> Result<(Operand, Kind), Diagnostic> {
        let (checked, kind) = self.value(operand)?;
        let Kind::Optional(inner) = kind else {
            return Self::fail(
                some.start,
                format!(
                    "some tests an optional, like optional_i32, but '{}' is {}",
                    self.text(operand.span),
                    kind.name()
                ),
            );
        };
        Ok((checked, *inner))
    }
}
