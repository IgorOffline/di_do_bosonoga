use crate::value::{Compare, Conversion, Operation, Value};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ExitStatus {
    Success,
    Failure,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Span {
    pub start: usize,
    pub end: usize,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Section {
    Global,
    Main,
    GfxInit,
    GfxFrame,
    GfxCleanup,
    GfxKeyDown,
    GfxMain,
}

impl Section {
    #[must_use]
    pub fn named(word: &str) -> Option<Section> {
        Some(match word {
            "bosonoga_global" => Section::Global,
            "bosonoga_main" => Section::Main,
            "gfx_init" => Section::GfxInit,
            "gfx_frame" => Section::GfxFrame,
            "gfx_cleanup" => Section::GfxCleanup,
            "gfx_key_down" => Section::GfxKeyDown,
            "gfx_main" => Section::GfxMain,
            _ => return None,
        })
    }

    #[must_use]
    pub fn word(self) -> &'static str {
        match self {
            Section::Global => "bosonoga_global",
            Section::Main => "bosonoga_main",
            Section::GfxInit => "gfx_init",
            Section::GfxFrame => "gfx_frame",
            Section::GfxCleanup => "gfx_cleanup",
            Section::GfxKeyDown => "gfx_key_down",
            Section::GfxMain => "gfx_main",
        }
    }

    #[must_use]
    pub fn is_gfx(self) -> bool {
        !matches!(self, Section::Global | Section::Main)
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum Term<'input> {
    Literal(Value),
    Shift(&'input str),
    Name(&'input str),
    Result(&'input str),
    GfxValue(&'input str),
}

#[derive(Clone, Debug, PartialEq)]
pub struct Operand<'input> {
    pub span: Span,
    pub term: Term<'input>,
}

pub type Named<'input> = (Span, &'input str);

#[derive(Debug, PartialEq)]
pub struct Comparison<'input> {
    pub negate: bool,
    pub left: Operand<'input>,
    pub right: Option<(Span, Compare, Operand<'input>)>,
}

pub type Condition<'input> = Vec<Vec<Comparison<'input>>>;

pub type Block<'input> = Vec<Statement<'input>>;

#[derive(Debug, PartialEq)]
pub enum Arguments<'input> {
    None,
    Of(Span, Vec<Operand<'input>>),
    At(Span, Operand<'input>),
}

#[derive(Debug, PartialEq)]
pub struct Call<'input> {
    pub name: Named<'input>,
    pub arguments: Arguments<'input>,
    pub taken: Span,
}

#[derive(Debug, PartialEq)]
pub enum Initial<'input> {
    Value {
        value: Operand<'input>,
        shift_or: Vec<(Span, Operand<'input>)>,
    },
    Conversion((Span, Conversion), Vec<Operand<'input>>),
    Call(Call<'input>),
}

#[derive(Debug, PartialEq)]
pub enum Draw {
    Clear,
    Rectangle,
    Hex,
    Circle,
    Text,
}

#[derive(Debug, PartialEq)]
pub enum Statement<'input> {
    Info(Vec<Operand<'input>>),
    Set {
        name: Named<'input>,
        operation: (Span, Operation),
        values: Vec<Operand<'input>>,
    },
    SetConversion {
        name: Named<'input>,
        conversion: (Span, Conversion),
        values: Vec<Operand<'input>>,
    },
    SetCall {
        name: Named<'input>,
        operation: (Span, Operation),
        call: Call<'input>,
    },
    Declare {
        name: Named<'input>,
        mutable: bool,
        initial: Initial<'input>,
    },
    If {
        branches: Vec<(Condition<'input>, Block<'input>)>,
        otherwise: Option<Block<'input>>,
    },
    While(Span, Condition<'input>, Block<'input>),
    AwaitNextFrame(Span),
    Draw {
        span: Span,
        draw: Draw,
        aligns: Option<(Named<'input>, Named<'input>)>,
        values: Vec<Operand<'input>>,
    },
    Nihil,
    Return(Span, ExitStatus),
    ReturnValues(Span, Vec<Operand<'input>>),
}

#[derive(Debug, PartialEq)]
pub enum Results {
    Void,
    Indexed(Vec<Span>),
}

#[derive(Debug, PartialEq)]
pub enum Item<'input> {
    Mod(Span, String),
    Const {
        public: Option<Span>,
        name: Named<'input>,
        value: Operand<'input>,
    },
    Def {
        public: Option<Span>,
        name: Named<'input>,
        parameters: Vec<Named<'input>>,
        results: (Span, Results),
        block: Block<'input>,
    },
    Array {
        public: Option<Span>,
        name: Named<'input>,
        kinds: Vec<Span>,
        rows: Vec<(Span, Vec<Operand<'input>>)>,
    },
    Section(Span, Section, Block<'input>, usize),
}

#[derive(Debug)]
pub struct File<'input> {
    pub items: Vec<Item<'input>>,
    pub end: usize,
}
