use std::cmp::Ordering;
use std::fmt::{self, Display, Formatter};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    I32,
    I64,
    U32,
    U64,
    F32,
    F64,
    Flags,
    Bool,
    Str,
}

impl Kind {
    #[must_use]
    pub fn name(self) -> &'static str {
        match self {
            Kind::I32 => "i32",
            Kind::I64 => "i64",
            Kind::U32 => "u32",
            Kind::U64 => "u64",
            Kind::F32 => "f32",
            Kind::F64 => "f64",
            Kind::Flags => "flags",
            Kind::Bool => "bool",
            Kind::Str => "str",
        }
    }

    #[must_use]
    pub fn is_integer(self) -> bool {
        matches!(self, Kind::I32 | Kind::I64 | Kind::U32 | Kind::U64)
    }

    #[must_use]
    pub fn is_real(self) -> bool {
        matches!(self, Kind::F32 | Kind::F64)
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum Value {
    I32(i32),
    I64(i64),
    U32(u32),
    U64(u64),
    F32(f32),
    F64(f64),
    Flags(u32),
    Bool(bool),
    Str(String),
}

impl Value {
    #[must_use]
    pub fn kind(&self) -> Kind {
        match self {
            Value::I32(_) => Kind::I32,
            Value::I64(_) => Kind::I64,
            Value::U32(_) => Kind::U32,
            Value::U64(_) => Kind::U64,
            Value::F32(_) => Kind::F32,
            Value::F64(_) => Kind::F64,
            Value::Flags(_) => Kind::Flags,
            Value::Bool(_) => Kind::Bool,
            Value::Str(_) => Kind::Str,
        }
    }

    #[must_use]
    pub fn whole(kind: Kind, whole: i128) -> Option<Value> {
        match kind {
            Kind::I32 => i32::try_from(whole).ok().map(Value::I32),
            Kind::I64 => i64::try_from(whole).ok().map(Value::I64),
            Kind::U32 => u32::try_from(whole).ok().map(Value::U32),
            Kind::U64 => u64::try_from(whole).ok().map(Value::U64),
            Kind::F32 | Kind::F64 | Kind::Flags | Kind::Bool | Kind::Str => None,
        }
    }

    #[must_use]
    pub fn real(kind: Kind, real: f64) -> Option<Value> {
        match kind {
            Kind::F64 if real.is_finite() => Some(Value::F64(real)),
            Kind::F32 if real.abs() <= f64::from(f32::MAX) => Some(Value::F32(narrow(real))),
            Kind::I32
            | Kind::I64
            | Kind::U32
            | Kind::U64
            | Kind::F32
            | Kind::F64
            | Kind::Flags
            | Kind::Bool
            | Kind::Str => None,
        }
    }

    fn as_whole(&self) -> Option<i128> {
        match *self {
            Value::I32(v) => Some(v.into()),
            Value::I64(v) => Some(v.into()),
            Value::U32(v) => Some(v.into()),
            Value::U64(v) => Some(v.into()),
            Value::F32(_) | Value::F64(_) | Value::Flags(_) | Value::Bool(_) | Value::Str(_) => {
                None
            }
        }
    }

    fn as_real(&self) -> Option<f64> {
        match *self {
            Value::F32(v) => Some(v.into()),
            Value::F64(v) => Some(v),
            Value::I32(_)
            | Value::I64(_)
            | Value::U32(_)
            | Value::U64(_)
            | Value::Flags(_)
            | Value::Bool(_)
            | Value::Str(_) => None,
        }
    }

    fn order(&self, other: &Value) -> Option<Ordering> {
        match (self.as_whole(), other.as_whole()) {
            (Some(a), Some(b)) => Some(a.cmp(&b)),
            _ => self.as_real()?.partial_cmp(&other.as_real()?),
        }
    }

    fn close(&self, other: &Value, tolerance: f64) -> bool {
        let (Some(a), Some(b)) = (self.as_real(), other.as_real()) else {
            return false;
        };
        let difference = (a - b).abs();
        let largest = a.abs().max(b.abs());
        difference <= tolerance || (largest > 0.0 && difference / largest <= tolerance)
    }

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

    fn equal(&self, other: &Value) -> bool {
        match (self, other) {
            (Value::Str(a), Value::Str(b)) => a == b,
            (Value::Bool(a), Value::Bool(b)) => a == b,
            _ => self.order(other) == Some(Ordering::Equal),
        }
    }
}

#[expect(
    clippy::as_conversions,
    clippy::cast_possible_truncation,
    reason = "archive_d narrows a parsed double to f32 with a C cast"
)]
fn narrow(real: f64) -> f32 {
    real as f32
}

impl Display for Value {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        match self {
            Value::I32(v) => write!(f, "{v}"),
            Value::I64(v) => write!(f, "{v}"),
            Value::U32(v) | Value::Flags(v) => write!(f, "{v}"),
            Value::U64(v) => write!(f, "{v}"),
            Value::F32(v) => f.write_str(&general(f64::from(*v), 6)),
            Value::F64(v) => f.write_str(&general(*v, 17)),
            Value::Bool(v) => f.write_str(if *v { "true" } else { "false" }),
            Value::Str(v) => f.write_str(v),
        }
    }
}

fn general(real: f64, precision: i32) -> String {
    let digits = usize::try_from(precision - 1).unwrap_or(0);
    let scientific = format!("{real:.digits$e}");
    let (mantissa, exponent_text) = scientific
        .split_once('e')
        .unwrap_or((scientific.as_str(), "0"));
    let exponent: i32 = exponent_text.parse().unwrap_or(0);
    let shown = if exponent < -4 || exponent >= precision {
        let sign = if exponent < 0 { '-' } else { '+' };
        format!(
            "{}e{sign}{:02}",
            trim_fraction(mantissa),
            exponent.unsigned_abs()
        )
    } else {
        let decimals = usize::try_from(precision - 1 - exponent).unwrap_or(0);
        trim_fraction(&format!("{real:.decimals$}")).to_owned()
    };
    shown.replace('.', "d")
}

fn trim_fraction(text: &str) -> &str {
    if text.contains('.') {
        text.trim_end_matches('0').trim_end_matches('.')
    } else {
        text
    }
}

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

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Operation {
    Copy,
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    Pow,
    Sqrt,
}

impl Operation {
    pub(crate) fn accepts(self, kind: Kind, count: usize) -> bool {
        match self {
            Self::Copy => count == 1,
            Self::Pow => kind.is_real() && count == 2,
            Self::Sqrt => kind.is_real() && count == 1,
            Self::Add | Self::Subtract | Self::Multiply | Self::Divide | Self::Modulo => {
                (kind.is_integer() || kind.is_real()) && count >= 2
            }
        }
    }

    pub(crate) fn evaluate(self, values: &[&Value]) -> Result<Value, &'static str> {
        let Some((first, rest)) = values.split_first() else {
            return Err("arithmetic needs operands");
        };
        if self == Self::Copy {
            return Ok((*first).clone());
        }
        let kind = first.kind();
        if let Some(mut whole) = first.as_whole() {
            for value in rest {
                let right = value.as_whole().ok_or("expected an integer")?;
                if matches!(self, Self::Divide | Self::Modulo) && right == 0 {
                    return Err("division by zero");
                }
                whole = match self {
                    Self::Add => whole.checked_add(right),
                    Self::Subtract => whole.checked_sub(right),
                    Self::Multiply => whole.checked_mul(right),
                    Self::Divide => whole.checked_div(right),
                    Self::Modulo => whole.checked_rem(right),
                    Self::Copy | Self::Pow | Self::Sqrt => None,
                }
                .ok_or("arithmetic overflow")?;
            }
            return Value::whole(kind, whole).ok_or("arithmetic result is out of range");
        }
        let mut real = first.as_real().ok_or("expected a real number")?;
        if self == Self::Sqrt {
            real = real.sqrt();
        }
        for value in rest {
            let right = value.as_real().ok_or("expected a real number")?;
            if matches!(self, Self::Divide | Self::Modulo)
                && right.classify() == std::num::FpCategory::Zero
            {
                return Err("division by zero");
            }
            real = match self {
                Self::Add => real + right,
                Self::Subtract => real - right,
                Self::Multiply => real * right,
                Self::Divide => real / right,
                Self::Modulo => real % right,
                Self::Pow => real.powf(right),
                Self::Copy | Self::Sqrt => return Err("invalid arithmetic operation"),
            };
        }
        Value::real(kind, real).ok_or("arithmetic result is out of range")
    }
}
