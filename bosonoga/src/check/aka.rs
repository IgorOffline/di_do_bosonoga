use super::{Aka, Checker, Declaration};
use crate::Diagnostic;
use crate::ast::{self, Named, Span, Term};
use crate::lexer::{UNE_PREFIX, is_type_name};
use crate::program::{self, Operand};
use crate::value::{Kind, Value};

// A name for the copy of a parameter in a message. What is before the type of
// une_i32 is a keyword.
pub(super) fn copy_name(base: &str) -> &str {
    if base == "une" { "copy" } else { base }
}

impl<'input> Checker<'_, 'input> {
    pub(super) fn aka_any(&self, name: &str) -> Option<usize> {
        for (index, aka) in self.akas.iter().enumerate() {
            if aka.name == name {
                return Some(index);
            }
        }
        None
    }

    pub(super) fn aka_name(&self, aka: usize) -> &'input str {
        match self.akas.get(aka) {
            Some(found) => found.name,
            None => "",
        }
    }

    // The name of a parameter without that of its aka_i32, as view of
    // view_fov, for the variable that it is converted to.
    pub(super) fn aka_base(&self, span: Span, aka: usize) -> &str {
        let suffix = self.aka_name(aka).len() + 1;
        let shown = self.text(span);
        copy_name(&shown[..shown.len().saturating_sub(suffix)])
    }

    // Declares an aka_i32 of a range, or of every i32, but some.
    pub(super) fn aka(
        &mut self,
        public: Option<Span>,
        (span, name): Named<'input>,
        range: Option<&(ast::Operand<'input>, ast::Operand<'input>)>,
        except: &[ast::Operand<'input>],
    ) -> Result<(), Diagnostic> {
        let is_public = self.public(public)?;
        self.aka_new_name((span, name))?;
        let mut bounds = None;
        if let Some((from, to)) = range {
            let first = self.aka_value(from)?;
            let end = self.aka_value(to)?;
            if first >= end {
                return Self::fail(
                    from.span.start,
                    format!(
                        "from {first}_i32 to {end}_i32 has no values, as from is inclusive and to is exclusive"
                    ),
                );
            }
            bounds = Some((first, end));
        }
        let mut values: Vec<i32> = Vec::new();
        for operand in except {
            let value = self.aka_value(operand)?;
            if let Some((from, to)) = bounds
                && (value < from || value >= to)
            {
                return Self::fail(
                    operand.span.start,
                    format!(
                        "{value}_i32 is not in from {from}_i32 to {to}_i32, so it needs no except"
                    ),
                );
            }
            if let Some(before) = values.last()
                && value <= *before
            {
                return Self::fail(
                    operand.span.start,
                    format!(
                        "the values of except are once each and increase, so {value}_i32 cannot follow {before}_i32"
                    ),
                );
            }
            values.push(value);
        }
        let aka = program::Aka {
            name: name.to_owned(),
            range: bounds,
            except: values,
        };
        Self::check_edges(&aka, except)?;
        self.program.akas.push(aka);
        let declaration: Declaration = self.declaration(span, is_public);
        self.akas.push(Aka { name, declaration });
        Ok(())
    }

    fn aka_new_name(&self, (span, name): Named<'input>) -> Result<(), Diagnostic> {
        let shown = self.text(span);
        let base = name.strip_prefix(UNE_PREFIX).unwrap_or(name);
        if is_type_name(base) {
            return Self::fail(span.start, format!("'{base}' is already a type"));
        }
        if base.contains('_') || base == "fn" {
            return Self::fail(
                span.start,
                format!(
                    "the name of an aka_i32 is one word, as parameters end with it, like view_fov of fov, so it cannot be '{shown}'"
                ),
            );
        }
        if self.aka_any(name).is_some() {
            return Self::fail(span.start, format!("aka_i32 '{shown}' already exists"));
        }
        self.new_name((span, name))
    }

    // A value of an aka_i32, which is known before the program runs.
    fn aka_value(&mut self, operand: &ast::Operand<'input>) -> Result<i32, Diagnostic> {
        match self.value_of_kind(operand, Kind::I32, "aka_i32")? {
            Operand::Literal(Value::I32(value)) => Ok(value),
            Operand::Literal(_) | Operand::Slot(_) | Operand::KeyHeld(_) => Self::fail(
                operand.span.start,
                format!(
                    "the values of an aka_i32 are literals or earlier constants, so '{}' cannot be one",
                    self.text(operand.span)
                ),
            ),
        }
    }

    // An except at the edge of a range is a smaller range, so that each range
    // has one form.
    fn check_edges(aka: &program::Aka, except: &[ast::Operand<'input>]) -> Result<(), Diagnostic> {
        let Some((from, to)) = aka.range else {
            return Ok(());
        };
        let mut first = from;
        while first < to && !aka.has(first) {
            first += 1;
        }
        let mut end = to;
        while end > first && !aka.has(end - 1) {
            end -= 1;
        }
        let at_start = first != from;
        let outermost = if at_start {
            except.first()
        } else {
            except.last()
        };
        let Some(operand) = outermost else {
            return Ok(());
        };
        if first == to {
            return Self::fail(
                operand.span.start,
                format!("{} has no values, as except has them all", aka.name),
            );
        }
        if at_start || end != to {
            return Self::fail(
                operand.span.start,
                format!(
                    "except is for values inside the range, and this one is at its edge, so write from {first}_i32 to {end}_i32"
                ),
            );
        }
        Ok(())
    }

    // The aka_i32 at the end of a name, with the text before and of it. That
    // of view_une_fov is une_fov, or else fov.
    fn aka_suffix<'a>(&self, name: &'a str) -> Option<(usize, &'a str, &'a str)> {
        let (base, suffix) = name.rsplit_once('_')?;
        if let Some(start) = base.strip_suffix("_une")
            && !start.is_empty()
        {
            let une_suffix = &name[start.len() + 1..];
            if let Some(index) = self.aka_any(une_suffix) {
                return Some((index, start, une_suffix));
            }
        }
        if base.is_empty() {
            return None;
        }
        let index = self.aka_any(suffix)?;
        Some((index, base, suffix))
    }

    // The aka_i32 that the name of a parameter ends with, as view_fov does.
    pub(super) fn parameter_aka(
        &mut self,
        (span, name): Named<'input>,
    ) -> Result<Option<usize>, Diagnostic> {
        let Some((index, base, suffix)) = self.aka_suffix(name) else {
            return Ok(None);
        };
        if base.ends_with("_optional") || base == "optional" {
            return Self::fail(
                span.start,
                format!("'{suffix}' is an aka_i32, which cannot be optional"),
            );
        }
        let start = span.end - suffix.len();
        self.check_not_hidden(
            Span {
                start,
                end: span.end,
            },
            suffix,
        )?;
        self.akas[index].declaration.used = true;
        Ok(Some(index))
    }

    // The slot and the aka_i32 of a parameter that has one, which is read.
    pub(super) fn aka_parameter(
        &mut self,
        operand: &ast::Operand<'input>,
    ) -> Option<(Operand, usize)> {
        let Term::Name(name) = operand.term else {
            return None;
        };
        let index = self.variable(name)?;
        let variable = &mut self.variables[index];
        let aka = variable.aka?;
        variable.declaration.used = true;
        Some((Operand::Slot(variable.slot), aka))
    }

    // An argument for a parameter of an aka_i32, and whether it is known to
    // be a value of it, which the call checks if it is not.
    pub(super) fn aka_argument(
        &mut self,
        operand: &ast::Operand<'input>,
        aka: usize,
        function: &str,
    ) -> Result<(Operand, bool), Diagnostic> {
        if let Some((slot, given)) = self.aka_parameter(operand)
            && given == aka
        {
            return Ok((slot, true));
        }
        let checked = self.value_of_kind(operand, Kind::I32, function)?;
        let mut known = None;
        if let Operand::Literal(Value::I32(value)) = &checked {
            known = Some(*value);
        } else if let Term::Name(name) = operand.term
            && let Some(index) = self.variable(name)
        {
            known = self.variables[index].known;
        }
        let Some(value) = known else {
            return Ok((checked, false));
        };
        if let Some(found) = self.program.akas.get(aka)
            && let Some(refusal) = found.refusal(function, value)
        {
            return Self::fail(operand.span.start, refusal);
        }
        Ok((checked, true))
    }

    // A parameter of an aka_i32 is not an i32 until it is converted.
    pub(super) fn check_not_aka(&self, span: Span, variable: usize) -> Result<(), Diagnostic> {
        let Some(aka) = self.variables[variable].aka else {
            return Ok(());
        };
        let shown = self.text(span);
        let base = self.aka_base(span, aka);
        Self::fail(
            span.start,
            format!(
                "'{shown}' is {}, an aka_i32, which is converted before its other uses, as in val {base} is to_i32 {shown}",
                self.aka_name(aka)
            ),
        )
    }
}
