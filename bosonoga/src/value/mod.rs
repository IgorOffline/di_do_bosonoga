mod compare;
mod conversion;
mod operation;

use std::fmt::{self, Display, Formatter};

pub use compare::Compare;
pub use conversion::Conversion;
pub use operation::Operation;

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
    // A value that may be absent, as Rust's Option, of a kind that
    // Kind::optional allows, as in opeto_i32.
    Optional(&'static Kind),
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
            Kind::Bool => "bu",
            Kind::InputKey => "input_key",
            Kind::Font => "font",
            Kind::Icon => "icon",
            Kind::Optional(inner) => inner.optional_name(),
        }
    }

    fn optional_name(self) -> &'static str {
        match self {
            Kind::I32 => "opeto_i32",
            Kind::I64 => "opeto_i64",
            Kind::U32 => "opeto_u32",
            Kind::U64 => "opeto_u64",
            Kind::F32 => "opeto_f32",
            Kind::F64 => "opeto_f64",
            Kind::Color => "opeto_color",
            Kind::Str => "opeto_str",
            Kind::Bool => "opeto_bu",
            Kind::Flags | Kind::InputKey | Kind::Font | Kind::Icon | Kind::Optional(_) => "opeto",
        }
    }

    // The optional kind of a kind, as opeto_i32 of i32. A number, bool,
    // str or color can be optional, but an optional or a handle cannot.
    #[must_use]
    pub fn optional(self) -> Option<Kind> {
        let inner: &'static Kind = match self {
            Kind::I32 => &Kind::I32,
            Kind::I64 => &Kind::I64,
            Kind::U32 => &Kind::U32,
            Kind::U64 => &Kind::U64,
            Kind::F32 => &Kind::F32,
            Kind::F64 => &Kind::F64,
            Kind::Color => &Kind::Color,
            Kind::Str => &Kind::Str,
            Kind::Bool => &Kind::Bool,
            Kind::Flags | Kind::InputKey | Kind::Font | Kind::Icon | Kind::Optional(_) => {
                return None;
            }
        };
        Some(Kind::Optional(inner))
    }

    #[must_use]
    pub fn is_optional(self) -> bool {
        matches!(self, Kind::Optional(_))
    }

    #[must_use]
    pub fn named(name: &str) -> Option<Kind> {
        if let Some(inner) = name.strip_prefix("opeto_") {
            return Kind::named_plain(inner)?.optional();
        }
        Kind::named_plain(name)
    }

    fn named_plain(name: &str) -> Option<Kind> {
        for kind in KINDS {
            if kind.name() == name {
                return Some(kind);
            }
        }
        None
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
    Font {
        index: usize,
        text: String,
    },
    Icon {
        index: usize,
        text: String,
    },
    // An optional of the kind inner, absent as nihil_i32 or present, as 7_opeto_i32.
    Optional {
        inner: Kind,
        value: Option<Box<Value>>,
    },
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
            Value::Optional { inner, .. } => match inner.optional() {
                Some(kind) => kind,
                None => *inner,
            },
        }
    }

    // A present optional of a value, as an i32 given where opeto_i32 is.
    #[must_use]
    pub fn present(value: Value) -> Value {
        Value::Optional {
            inner: value.kind(),
            value: Some(Box::new(value)),
        }
    }

    #[must_use]
    pub fn whole(kind: Kind, whole: i128) -> Option<Value> {
        match kind {
            Kind::I32 => match i32::try_from(whole) {
                Ok(number) => Some(Value::I32(number)),
                Err(_) => None,
            },
            Kind::I64 => match i64::try_from(whole) {
                Ok(number) => Some(Value::I64(number)),
                Err(_) => None,
            },
            Kind::U32 => match u32::try_from(whole) {
                Ok(number) => Some(Value::U32(number)),
                Err(_) => None,
            },
            Kind::U64 => match u64::try_from(whole) {
                Ok(number) => Some(Value::U64(number)),
                Err(_) => None,
            },
            Kind::F32
            | Kind::F64
            | Kind::Flags
            | Kind::Color
            | Kind::Str
            | Kind::Bool
            | Kind::InputKey
            | Kind::Font
            | Kind::Icon
            | Kind::Optional(_) => None,
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
            | Kind::Icon
            | Kind::Optional(_) => None,
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
            | Value::Icon { .. }
            | Value::Optional { .. } => None,
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
            | Value::Icon { .. }
            | Value::Optional { .. } => None,
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
            | Value::Icon { .. }
            | Value::Optional { .. } => 0.0,
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
            | Value::Icon { .. }
            | Value::Optional { .. } => 0,
        }
    }

    fn as_number(&self) -> f64 {
        if let Some(real) = self.as_real() {
            return real;
        }
        if let Some(whole) = self.as_whole() {
            return whole_to_real(whole);
        }
        0.0
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
            Value::Color(v) => {
                let [_, red, green, blue] = v.to_be_bytes();
                write!(f, "{red:02X}{green:02X}{blue:02X}")?;
                if !crate::gfx::is_opaque(*v) {
                    write!(f, "{:02X}", crate::gfx::alpha(*v))?;
                }
                Ok(())
            }
            Value::Bool(v) => f.write_str(if *v { "tara" } else { "fales" }),
            Value::Str(v) | Value::Font { text: v, .. } | Value::Icon { text: v, .. } => {
                f.write_str(v)
            }
            Value::InputKey(v) => f.write_str(crate::key::name(*v).unwrap_or_default()),
            Value::Optional { value: Some(v), .. } => write!(f, "{v}"),
            Value::Optional { value: None, .. } => f.write_str("nihil"),
        }
    }
}

pub(crate) fn general(real: f64, precision: i32) -> String {
    let digits = usize::try_from(precision - 1).unwrap_or(0);
    let scientific = format!("{real:.digits$e}");
    let mut mantissa = scientific.as_str();
    let mut exponent = 0;
    if let Some((before, after)) = scientific.split_once('e') {
        mantissa = before;
        exponent = after.parse().unwrap_or(0);
    }
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
