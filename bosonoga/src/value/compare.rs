use std::cmp::Ordering;

use super::Value;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Compare {
    Gt,
    Lt,
    Ge,
    Le,
    Eq,
    NotEq,
    Eq3,
    Eq6,
    NotEq3,
    NotEq6,
    ShiftHas,
}

impl Compare {
    #[must_use]
    pub fn named(word: &str) -> Option<Compare> {
        Some(match word {
            "gt" => Compare::Gt,
            "lt" => Compare::Lt,
            "ge" => Compare::Ge,
            "le" => Compare::Le,
            "eq" => Compare::Eq,
            "not_eq" => Compare::NotEq,
            "eq_3" => Compare::Eq3,
            "eq_6" => Compare::Eq6,
            "not_eq_3" => Compare::NotEq3,
            "not_eq_6" => Compare::NotEq6,
            "shift_has" => Compare::ShiftHas,
            _ => return None,
        })
    }
}

impl Value {
    #[must_use]
    pub fn compare(&self, compare: Compare, other: &Value) -> bool {
        match compare {
            Compare::Gt => self.order(other) == Some(Ordering::Greater),
            Compare::Lt => self.order(other) == Some(Ordering::Less),
            Compare::Ge => matches!(self.order(other), Some(Ordering::Greater | Ordering::Equal)),
            Compare::Le => matches!(self.order(other), Some(Ordering::Less | Ordering::Equal)),
            Compare::Eq => self.equal(other),
            Compare::NotEq => !self.equal(other),
            Compare::Eq3 => self.close(other, 1e-3),
            Compare::Eq6 => self.close(other, 1e-6),
            Compare::NotEq3 => !self.close(other, 1e-3),
            Compare::NotEq6 => !self.close(other, 1e-6),
            Compare::ShiftHas => match (self, other) {
                (Value::Flags(a), Value::Flags(b)) => a & b == *b,
                _ => false,
            },
        }
    }

    fn order(&self, other: &Value) -> Option<Ordering> {
        if let Some(a) = self.as_whole()
            && let Some(b) = other.as_whole()
        {
            return Some(a.cmp(&b));
        }
        let a = self.as_real()?;
        let b = other.as_real()?;
        a.partial_cmp(&b)
    }

    // Whether two reals are equal to 3 or 6 decimals, as eq_3 and eq_6 say:
    // their difference is at most the tolerance, however large they are.
    fn close(&self, other: &Value, tolerance: f64) -> bool {
        let (Some(a), Some(b)) = (self.as_real(), other.as_real()) else {
            return false;
        };
        (a - b).abs() <= tolerance
    }

    fn equal(&self, other: &Value) -> bool {
        match (self, other) {
            (Value::Str(a), Value::Str(b)) => a == b,
            (Value::Bool(a), Value::Bool(b)) => a == b,
            (Value::InputKey(a), Value::InputKey(b)) => a == b,
            (Value::Font { index: a, .. }, Value::Font { index: b, .. })
            | (Value::Icon { index: a, .. }, Value::Icon { index: b, .. }) => a == b,
            (Value::Optional { value: left, .. }, Value::Optional { value: right, .. }) => {
                match (left, right) {
                    (Some(present), Some(given)) => present.equal(given),
                    (None, None) => true,
                    (Some(_), None) | (None, Some(_)) => false,
                }
            }
            _ => self.order(other) == Some(Ordering::Equal),
        }
    }
}
