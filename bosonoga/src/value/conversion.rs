use std::num::FpCategory;

use super::{Kind, Value, WHOLE_REAL_MAX, real_to_whole};

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
    // A present optional of a value of the kind, as to_optional_i32 of i32.
    ToOptional(Kind),
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
        for (name, conversion) in CONVERSIONS {
            if name == word {
                return Some(conversion);
            }
        }
        if let Some(Kind::Optional(inner)) = word.strip_prefix("to_").and_then(Kind::named) {
            return Some(Conversion::ToOptional(*inner));
        }
        None
    }

    #[must_use]
    pub fn word(self) -> &'static str {
        if let Conversion::ToOptional(inner) = self {
            return match inner {
                Kind::I32 => "to_optional_i32",
                Kind::I64 => "to_optional_i64",
                Kind::U32 => "to_optional_u32",
                Kind::U64 => "to_optional_u64",
                Kind::F32 => "to_optional_f32",
                Kind::F64 => "to_optional_f64",
                Kind::Color => "to_optional_color",
                Kind::Str => "to_optional_str",
                Kind::Bool => "to_optional_bu",
                Kind::Flags | Kind::InputKey | Kind::Font | Kind::Icon | Kind::Optional(_) => {
                    "to_optional"
                }
            };
        }
        for (name, conversion) in CONVERSIONS {
            if conversion == self {
                return name;
            }
        }
        ""
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
            | Self::Hypot
            | Self::ToOptional(_) => None,
        }
    }

    pub(crate) fn result_kind(self, source: Kind) -> Kind {
        if let Self::ToOptional(inner) = self {
            return inner.optional().unwrap_or(inner);
        }
        self.target().unwrap_or(source)
    }

    pub(crate) fn evaluate(self, left: &Value, right: Option<&Value>) -> Result<Value, String> {
        if let Self::ToOptional(_) = self {
            return Ok(Value::present(left.clone()));
        }
        if let Some(kind) = self.target() {
            return convert(kind, left);
        }
        let kind = left.kind();
        if let Some(a) = left.as_whole() {
            let mut b = 0;
            if let Some(other) = right
                && let Some(whole) = other.as_whole()
            {
                b = whole;
            }
            let whole = if self == Self::Min {
                a.min(b)
            } else if self == Self::Max {
                a.max(b)
            } else {
                a.abs()
            };
            return match Value::whole(kind, whole) {
                Some(value) => Ok(value),
                None => Err(outside(kind)),
            };
        }
        let a = left.as_number();
        let mut b = 0.0;
        if let Some(other) = right
            && let Some(real) = other.as_real()
        {
            b = real;
        }
        if let Some(domain) = self.domain_error(a, b) {
            return Err(domain.to_owned());
        }
        match Value::real(kind, self.real(a, b)) {
            Some(value) => Ok(value),
            None => Err(outside(kind)),
        }
    }

    fn domain_error(self, a: f64, b: f64) -> Option<&'static str> {
        if self == Self::Asin && !(-1.0..=1.0).contains(&a) {
            Some("math_to_asin needs a value from -1 to 1")
        } else if self == Self::Acos && !(-1.0..=1.0).contains(&a) {
            Some("math_to_acos needs a value from -1 to 1")
        } else if self == Self::Sqrt && a < 0.0 {
            Some("math_to_sqrt of a negative number")
        } else if self == Self::Pow && a < 0.0 && !is_zero(b.fract()) {
            Some("math_to_pow of a negative number to a fractional power")
        } else if self == Self::Pow && is_zero(a) && b < 0.0 {
            Some("math_to_pow of zero to a negative power is division by zero")
        } else if (self == Self::Log || self == Self::Log10) && a <= 0.0 {
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
            Self::ToI32
            | Self::ToI64
            | Self::ToU32
            | Self::ToU64
            | Self::ToF32
            | Self::ToF64
            | Self::ToOptional(_) => a,
        }
    }
}

fn is_zero(x: f64) -> bool {
    x.classify() == FpCategory::Zero
}

fn outside(kind: Kind) -> String {
    format!("the result is outside the {} range", kind.name())
}

fn convert(kind: Kind, value: &Value) -> Result<Value, String> {
    let converted = if kind.is_real() {
        Value::real(kind, value.as_number())
    } else if let Some(whole) = value.as_whole() {
        Value::whole(kind, whole)
    } else {
        let whole = value.as_number().trunc();
        if whole.abs() < WHOLE_REAL_MAX {
            Value::whole(kind, real_to_whole(whole))
        } else {
            None
        }
    };
    match converted {
        Some(result) => Ok(result),
        None => Err(format!("the value is outside the {} range", kind.name())),
    }
}
