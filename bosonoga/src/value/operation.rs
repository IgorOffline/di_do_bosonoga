use std::num::FpCategory;

use super::{Kind, Value, general};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Operation {
    Copy,
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
}

impl Operation {
    #[must_use]
    pub fn named(word: &str) -> Option<Operation> {
        Some(match word {
            "tu" => Self::Copy,
            "tu_add" => Self::Add,
            "tu_subtract" => Self::Subtract,
            "tu_multiply" => Self::Multiply,
            "tu_divide" => Self::Divide,
            "tu_mod" => Self::Modulo,
            _ => return None,
        })
    }

    fn divides(self) -> bool {
        self == Self::Divide || self == Self::Modulo
    }

    pub(crate) fn evaluate(self, values: &[&Value]) -> Result<Value, String> {
        let Some((first, rest)) = values.split_first() else {
            return Err("arithmetic needs operands".to_owned());
        };
        if self == Self::Copy {
            return Ok((*first).clone());
        }
        if let Some(whole) = first.as_whole() {
            return self.evaluate_whole(first.kind(), whole, rest);
        }
        match first.as_real() {
            Some(real) => self.evaluate_real(first.kind(), real, rest),
            None => Err("expected a real number".to_owned()),
        }
    }

    fn evaluate_whole(self, kind: Kind, first: i128, rest: &[&Value]) -> Result<Value, String> {
        let mut whole = first;
        for value in rest {
            let Some(right) = value.as_whole() else {
                return Err("expected an integer".to_owned());
            };
            if self.divides() && right == 0 {
                return Err("division by zero".to_owned());
            }
            let Some(result) = self.whole_step(whole, right) else {
                return Err(format!("the result is outside the {} range", kind.name()));
            };
            whole = result;
        }
        match Value::whole(kind, whole) {
            Some(value) => Ok(value),
            None => Err(format!(
                "the result ({whole}) is outside the {} range",
                kind.name()
            )),
        }
    }

    fn whole_step(self, left: i128, right: i128) -> Option<i128> {
        match self {
            Self::Add => left.checked_add(right),
            Self::Subtract => left.checked_sub(right),
            Self::Multiply => left.checked_mul(right),
            Self::Divide if right == -1 => left.checked_neg(),
            Self::Modulo if right == -1 => Some(0),
            Self::Divide => left.checked_div(right),
            Self::Modulo => left.checked_rem(right),
            Self::Copy => None,
        }
    }

    fn evaluate_real(self, kind: Kind, first: f64, rest: &[&Value]) -> Result<Value, String> {
        let mut real = first;
        for value in rest {
            let Some(right) = value.as_real() else {
                return Err("expected a real number".to_owned());
            };
            if self.divides() && right.classify() == FpCategory::Zero {
                return Err("division by zero".to_owned());
            }
            real = self.real_step(real, right);
        }
        match Value::real(kind, real) {
            Some(value) => Ok(value),
            None => Err(format!(
                "the result ({}) is outside the {} range",
                general(real, 6),
                kind.name()
            )),
        }
    }

    fn real_step(self, left: f64, right: f64) -> f64 {
        match self {
            Self::Add => left + right,
            Self::Subtract => left - right,
            Self::Multiply => left * right,
            Self::Divide => left / right,
            Self::Modulo => left % right,
            Self::Copy => right,
        }
    }
}
