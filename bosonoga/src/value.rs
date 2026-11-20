use std::cmp::Ordering;
use std::fmt::{self, Display, Formatter};

const WHOLE_REAL_MAX: f64 = 36_893_488_147_419_103_232.0;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    I32,
    I64,
    U32,
    U64,
    F32,
    F64,
    Flags,
    Color,
    Str,
    Bool,
    InputKey,
    Font,
    Icon,
}

const KINDS: [Kind; 13] = [
    Kind::I32,
    Kind::I64,
    Kind::U32,
    Kind::U64,
    Kind::F32,
    Kind::F64,
    Kind::Flags,
    Kind::Color,
    Kind::Str,
    Kind::Bool,
    Kind::InputKey,
    Kind::Font,
    Kind::Icon,
];

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
            Kind::Color => "color",
            Kind::Str => "str",
            Kind::Bool => "bool",
            Kind::InputKey => "input_key",
            Kind::Font => "font",
            Kind::Icon => "icon",
        }
    }

    #[must_use]
    pub fn named(name: &str) -> Option<Kind> {
        KINDS.into_iter().find(|kind| kind.name() == name)
    }

    #[must_use]
    pub fn is_integer(self) -> bool {
        matches!(self, Kind::I32 | Kind::I64 | Kind::U32 | Kind::U64)
    }

    #[must_use]
    pub fn is_real(self) -> bool {
        matches!(self, Kind::F32 | Kind::F64)
    }

    #[must_use]
    pub fn is_number(self) -> bool {
        self.is_integer() || self.is_real()
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
    Color(u32),
    Str(String),
    Bool(bool),
    InputKey(u32),
    Font { index: usize, text: String },
    Icon { index: usize, text: String },
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
            Value::Color(_) => Kind::Color,
            Value::Str(_) => Kind::Str,
            Value::Bool(_) => Kind::Bool,
            Value::InputKey(_) => Kind::InputKey,
            Value::Font { .. } => Kind::Font,
            Value::Icon { .. } => Kind::Icon,
        }
    }

    #[must_use]
    pub fn whole(kind: Kind, whole: i128) -> Option<Value> {
        match kind {
            Kind::I32 => i32::try_from(whole).ok().map(Value::I32),
            Kind::I64 => i64::try_from(whole).ok().map(Value::I64),
            Kind::U32 => u32::try_from(whole).ok().map(Value::U32),
            Kind::U64 => u64::try_from(whole).ok().map(Value::U64),
            Kind::F32
            | Kind::F64
            | Kind::Flags
            | Kind::Color
            | Kind::Str
            | Kind::Bool
            | Kind::InputKey
            | Kind::Font
            | Kind::Icon => None,
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
            | Kind::Color
            | Kind::Str
            | Kind::Bool
            | Kind::InputKey
            | Kind::Font
            | Kind::Icon => None,
        }
    }

    fn one(kind: Kind, one: bool) -> Option<Value> {
        if kind.is_integer() {
            Value::whole(kind, i128::from(one))
        } else {
            Value::real(kind, f64::from(u8::from(one)))
        }
    }

    pub(crate) fn as_whole(&self) -> Option<i128> {
        match *self {
            Value::I32(v) => Some(v.into()),
            Value::I64(v) => Some(v.into()),
            Value::U32(v) => Some(v.into()),
            Value::U64(v) => Some(v.into()),
            Value::F32(_)
            | Value::F64(_)
            | Value::Flags(_)
            | Value::Color(_)
            | Value::Str(_)
            | Value::Bool(_)
            | Value::InputKey(_)
            | Value::Font { .. }
            | Value::Icon { .. } => None,
        }
    }

    pub(crate) fn as_real(&self) -> Option<f64> {
        match *self {
            Value::F32(v) => Some(v.into()),
            Value::F64(v) => Some(v),
            Value::I32(_)
            | Value::I64(_)
            | Value::U32(_)
            | Value::U64(_)
            | Value::Flags(_)
            | Value::Color(_)
            | Value::Str(_)
            | Value::Bool(_)
            | Value::InputKey(_)
            | Value::Font { .. }
            | Value::Icon { .. } => None,
        }
    }

    pub(crate) fn as_f32(&self) -> f32 {
        match *self {
            Value::F32(v) => v,
            Value::I32(_)
            | Value::I64(_)
            | Value::U32(_)
            | Value::U64(_)
            | Value::F64(_)
            | Value::Flags(_)
            | Value::Color(_)
            | Value::Str(_)
            | Value::Bool(_)
            | Value::InputKey(_)
            | Value::Font { .. }
            | Value::Icon { .. } => 0.0,
        }
    }

    pub(crate) fn as_color(&self) -> u32 {
        match *self {
            Value::Color(v) => v,
            Value::I32(_)
            | Value::I64(_)
            | Value::U32(_)
            | Value::U64(_)
            | Value::F32(_)
            | Value::F64(_)
            | Value::Flags(_)
            | Value::Str(_)
            | Value::Bool(_)
            | Value::InputKey(_)
            | Value::Font { .. }
            | Value::Icon { .. } => 0,
        }
    }

    fn as_number(&self) -> f64 {
        self.as_real()
            .or_else(|| self.as_whole().map(whole_to_real))
            .unwrap_or(0.0)
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
            (Value::InputKey(a), Value::InputKey(b)) => a == b,
            (Value::Font { index: a, .. }, Value::Font { index: b, .. })
            | (Value::Icon { index: a, .. }, Value::Icon { index: b, .. }) => a == b,
            _ => self.order(other) == Some(Ordering::Equal),
        }
    }
}

#[expect(
    clippy::as_conversions,
    clippy::cast_possible_truncation,
    reason = "archive_d narrows a double to f32 with a C cast"
)]
fn narrow(real: f64) -> f32 {
    real as f32
}

#[expect(
    clippy::as_conversions,
    clippy::cast_precision_loss,
    reason = "archive_d widens an integer to a double with a C cast"
)]
fn whole_to_real(whole: i128) -> f64 {
    whole as f64
}

#[expect(
    clippy::as_conversions,
    clippy::cast_possible_truncation,
    reason = "the real is whole and below 2 to the 65"
)]
fn real_to_whole(real: f64) -> i128 {
    real as i128
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
            Value::Color(v) => write!(f, "{v:06X}"),
            Value::Bool(v) => f.write_str(if *v { "true" } else { "false" }),
            Value::Str(v) | Value::Font { text: v, .. } | Value::Icon { text: v, .. } => {
                f.write_str(v)
            }
            Value::InputKey(v) => f.write_str(crate::key::name(*v).unwrap_or_default()),
        }
    }
}

pub(crate) fn general(real: f64, precision: i32) -> String {
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
}

impl Operation {
    #[must_use]
    pub fn named(word: &str) -> Option<Operation> {
        Some(match word {
            "to" => Self::Copy,
            "to_add" => Self::Add,
            "to_subtract" => Self::Subtract,
            "to_multiply" => Self::Multiply,
            "to_divide" => Self::Divide,
            "to_mod" => Self::Modulo,
            _ => return None,
        })
    }

    pub(crate) fn identity(self, kind: Kind) -> Option<Value> {
        match self {
            Self::Subtract => Value::one(kind, false),
            Self::Divide => Value::one(kind, true),
            Self::Copy | Self::Add | Self::Multiply | Self::Modulo => None,
        }
    }

    pub(crate) fn evaluate(self, values: &[&Value]) -> Result<Value, String> {
        let Some((first, rest)) = values.split_first() else {
            return Err("arithmetic needs operands".to_owned());
        };
        if self == Self::Copy {
            return Ok((*first).clone());
        }
        let kind = first.kind();
        let outside = || format!("the result is outside the {} range", kind.name());
        if let Some(mut whole) = first.as_whole() {
            for value in rest {
                let right = value.as_whole().ok_or("expected an integer")?;
                if matches!(self, Self::Divide | Self::Modulo) && right == 0 {
                    return Err("division by zero".to_owned());
                }
                whole = match self {
                    Self::Add => whole.checked_add(right),
                    Self::Subtract => whole.checked_sub(right),
                    Self::Multiply => whole.checked_mul(right),
                    Self::Divide if right == -1 => whole.checked_neg(),
                    Self::Modulo if right == -1 => Some(0),
                    Self::Divide => whole.checked_div(right),
                    Self::Modulo => whole.checked_rem(right),
                    Self::Copy => None,
                }
                .ok_or_else(outside)?;
            }
            return Value::whole(kind, whole).ok_or_else(|| {
                format!("the result ({whole}) is outside the {} range", kind.name())
            });
        }
        let mut real = first.as_real().ok_or("expected a real number")?;
        for value in rest {
            let right = value.as_real().ok_or("expected a real number")?;
            if matches!(self, Self::Divide | Self::Modulo)
                && right.classify() == std::num::FpCategory::Zero
            {
                return Err("division by zero".to_owned());
            }
            real = match self {
                Self::Add => real + right,
                Self::Subtract => real - right,
                Self::Multiply => real * right,
                Self::Divide => real / right,
                Self::Modulo => real % right,
                Self::Copy => right,
            };
        }
        Value::real(kind, real).ok_or_else(|| {
            format!(
                "the result ({}) is outside the {} range",
                general(real, 6),
                kind.name()
            )
        })
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Conversion {
    ToI32,
    ToI64,
    ToU32,
    ToU64,
    ToF32,
    ToF64,
    Abs,
    Min,
    Max,
    Sin,
    Cos,
    Atan2,
    Sqrt,
    Pow,
    Floor,
    Ceil,
    Round,
    Log,
    Log10,
    Exp,
    Asin,
    Acos,
    Tan,
    Log1p,
    Expm1,
    Hypot,
}

const CONVERSIONS: [(&str, Conversion); 26] = [
    ("to_i32", Conversion::ToI32),
    ("to_i64", Conversion::ToI64),
    ("to_u32", Conversion::ToU32),
    ("to_u64", Conversion::ToU64),
    ("to_f32", Conversion::ToF32),
    ("to_f64", Conversion::ToF64),
    ("math_to_abs", Conversion::Abs),
    ("math_to_min", Conversion::Min),
    ("math_to_max", Conversion::Max),
    ("math_to_sin", Conversion::Sin),
    ("math_to_cos", Conversion::Cos),
    ("math_to_atan2", Conversion::Atan2),
    ("math_to_sqrt", Conversion::Sqrt),
    ("math_to_pow", Conversion::Pow),
    ("math_to_floor", Conversion::Floor),
    ("math_to_ceil", Conversion::Ceil),
    ("math_to_round", Conversion::Round),
    ("math_to_log", Conversion::Log),
    ("math_to_log10", Conversion::Log10),
    ("math_to_exp", Conversion::Exp),
    ("math_to_asin", Conversion::Asin),
    ("math_to_acos", Conversion::Acos),
    ("math_to_tan", Conversion::Tan),
    ("math_to_log1p", Conversion::Log1p),
    ("math_to_expm1", Conversion::Expm1),
    ("math_to_hypot", Conversion::Hypot),
];

impl Conversion {
    #[must_use]
    pub fn named(word: &str) -> Option<Conversion> {
        CONVERSIONS
            .into_iter()
            .find(|(name, _)| *name == word)
            .map(|(_, conversion)| conversion)
    }

    #[must_use]
    pub fn word(self) -> &'static str {
        CONVERSIONS
            .into_iter()
            .find(|(_, conversion)| *conversion == self)
            .map_or("", |(name, _)| name)
    }

    pub(crate) fn is_binary(self) -> bool {
        matches!(
            self,
            Self::Min | Self::Max | Self::Atan2 | Self::Pow | Self::Hypot
        )
    }

    pub(crate) fn accepts_integer(self) -> bool {
        self.target().is_some() || matches!(self, Self::Abs | Self::Min | Self::Max)
    }

    fn target(self) -> Option<Kind> {
        match self {
            Self::ToI32 => Some(Kind::I32),
            Self::ToI64 => Some(Kind::I64),
            Self::ToU32 => Some(Kind::U32),
            Self::ToU64 => Some(Kind::U64),
            Self::ToF32 => Some(Kind::F32),
            Self::ToF64 => Some(Kind::F64),
            Self::Abs
            | Self::Min
            | Self::Max
            | Self::Sin
            | Self::Cos
            | Self::Atan2
            | Self::Sqrt
            | Self::Pow
            | Self::Floor
            | Self::Ceil
            | Self::Round
            | Self::Log
            | Self::Log10
            | Self::Exp
            | Self::Asin
            | Self::Acos
            | Self::Tan
            | Self::Log1p
            | Self::Expm1
            | Self::Hypot => None,
        }
    }

    pub(crate) fn result_kind(self, source: Kind) -> Kind {
        self.target().unwrap_or(source)
    }

    pub(crate) fn evaluate(self, left: &Value, right: Option<&Value>) -> Result<Value, String> {
        if let Some(kind) = self.target() {
            let converted = if kind.is_real() {
                Value::real(kind, left.as_number())
            } else if let Some(whole) = left.as_whole() {
                Value::whole(kind, whole)
            } else {
                let whole = left.as_number().trunc();
                if whole.abs() < WHOLE_REAL_MAX {
                    Value::whole(kind, real_to_whole(whole))
                } else {
                    None
                }
            };
            return converted
                .ok_or_else(|| format!("the value is outside the {} range", kind.name()));
        }
        let kind = left.kind();
        let outside = || format!("the result is outside the {} range", kind.name());
        if let Some(a) = left.as_whole() {
            let b = right.and_then(Value::as_whole).unwrap_or(0);
            let whole = if self == Self::Min {
                a.min(b)
            } else if self == Self::Max {
                a.max(b)
            } else {
                a.abs()
            };
            return Value::whole(kind, whole).ok_or_else(outside);
        }
        let a = left.as_number();
        let b = right.and_then(Value::as_real).unwrap_or(0.0);
        if let Some(domain) = self.domain_error(a, b) {
            return Err(domain.to_owned());
        }
        Value::real(kind, self.real(a, b)).ok_or_else(outside)
    }

    fn domain_error(self, a: f64, b: f64) -> Option<&'static str> {
        let zero = |x: f64| x.classify() == std::num::FpCategory::Zero;
        if self == Self::Asin && !(-1.0..=1.0).contains(&a) {
            Some("math_to_asin needs a value from -1 to 1")
        } else if self == Self::Acos && !(-1.0..=1.0).contains(&a) {
            Some("math_to_acos needs a value from -1 to 1")
        } else if self == Self::Sqrt && a < 0.0 {
            Some("math_to_sqrt of a negative number")
        } else if self == Self::Pow && a < 0.0 && !zero(b.fract()) {
            Some("math_to_pow of a negative number to a fractional power")
        } else if self == Self::Pow && zero(a) && b < 0.0 {
            Some("math_to_pow of zero to a negative power is division by zero")
        } else if matches!(self, Self::Log | Self::Log10) && a <= 0.0 {
            Some("logarithms need a positive value")
        } else if self == Self::Log1p && a <= -1.0 {
            Some("math_to_log1p needs a value greater than -1")
        } else {
            None
        }
    }

    fn real(self, a: f64, b: f64) -> f64 {
        match self {
            Self::Abs => a.abs(),
            Self::Min => a.min(b),
            Self::Max => a.max(b),
            Self::Floor => a.floor(),
            Self::Ceil => a.ceil(),
            Self::Round => a.round(),
            Self::Sin => a.sin(),
            Self::Cos => a.cos(),
            Self::Tan => a.tan(),
            Self::Asin => a.asin(),
            Self::Acos => a.acos(),
            Self::Atan2 => a.atan2(b),
            Self::Sqrt => a.sqrt(),
            Self::Pow => a.powf(b),
            Self::Log => a.ln(),
            Self::Log10 => a.log10(),
            Self::Exp => a.exp(),
            Self::Log1p => a.ln_1p(),
            Self::Expm1 => a.exp_m1(),
            Self::Hypot => a.hypot(b),
            Self::ToI32 | Self::ToI64 | Self::ToU32 | Self::ToU64 | Self::ToF32 | Self::ToF64 => a,
        }
    }
}
