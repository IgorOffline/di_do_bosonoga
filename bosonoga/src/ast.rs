use crate::gfx::Align;
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
    GfxKeyUp,
    GfxMain,
}

impl Section {
    #[must_use]
    pub fn named(word: &str) -> Option<Section> {
        Some(match word {
            "global" => Section::Global,
            "main" => Section::Main,
            "gfx_init" => Section::GfxInit,
            "gfx_frame" => Section::GfxFrame,
            "gfx_cleanup" => Section::GfxCleanup,
            "gfx_key_down" => Section::GfxKeyDown,
            "gfx_key_up" => Section::GfxKeyUp,
            "gfx_main" => Section::GfxMain,
            _ => return None,
        })
    }

    #[must_use]
    pub fn word(self) -> &'static str {
        match self {
            Section::Global => "global",
            Section::Main => "main",
            Section::GfxInit => "gfx_init",
            Section::GfxFrame => "gfx_frame",
            Section::GfxCleanup => "gfx_cleanup",
            Section::GfxKeyDown => "gfx_key_down",
            Section::GfxKeyUp => "gfx_key_up",
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
    // gfx_key_held key_forward, whether a key is down.
    KeyHeld(Named<'input>),
}

#[derive(Clone, Debug, PartialEq)]
pub struct Operand<'input> {
    pub span: Span,
    pub term: Term<'input>,
}

pub type Named<'input> = (Span, &'input str);

// A column of an array or a value of a def or a given operation, which is
// named with its type, as in count_i32, or, by mistake, a literal, as the
// 0_i32 of a value that was numbered.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Column<'input> {
    Named(Named<'input>),
    Literal(Span),
}

impl Column<'_> {
    #[must_use]
    pub fn span(&self) -> Span {
        match self {
            Column::Named((span, _)) | Column::Literal(span) => *span,
        }
    }
}

// A value that with ret takes, as a val of its own name or of the name after
// as.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Taken<'input> {
    pub column: Column<'input>,
    pub binding: Option<Named<'input>>,
}

// The some of a comparison, as in some count, or some count as number,
// which names the value of the optional for the block of an if.
#[derive(Debug, PartialEq)]
pub struct Presence<'input> {
    pub span: Span,
    pub binding: Option<Named<'input>>,
}

#[derive(Debug, PartialEq)]
pub struct Comparison<'input> {
    pub negate: bool,
    pub presence: Option<Presence<'input>>,
    pub left: Operand<'input>,
    pub right: Option<(Span, Compare, Operand<'input>)>,
}

pub type Condition<'input> = Vec<Vec<Comparison<'input>>>;

// A statement and where it starts.
#[derive(Debug, PartialEq)]
pub struct Located<'input> {
    pub start: usize,
    pub statement: Statement<'input>,
}

pub type Block<'input> = Vec<Located<'input>>;

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
    pub taken: Column<'input>,
}

// The values of a call statement, taken for its block, as in
// with ret count_i32 marked_bool as seen di ... do.
#[derive(Debug, PartialEq)]
pub struct Taking<'input> {
    pub with: Span,
    pub results: Vec<Taken<'input>>,
    pub block: Block<'input>,
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

// A step of then, which starts from the value so far, as in
// var x is 1_i32 then to_add 2_i32 then to_f32.
#[derive(Debug, PartialEq)]
pub enum Then<'input> {
    Operation((Span, Operation), Vec<Operand<'input>>),
    Conversion((Span, Conversion), Vec<Operand<'input>>),
    Join(Span, Vec<Operand<'input>>),
}

#[derive(Debug, PartialEq)]
pub enum Draw {
    Clear,
    Rectangle,
    Hex,
    Circle,
    Text,
    Icon,
    Cube,
    CubeWires,
    Grid,
}

impl Draw {
    // Whether it draws in the 3D of a gfx_camera_3d block.
    #[must_use]
    pub fn is_3d(&self) -> bool {
        match self {
            Draw::Cube | Draw::CubeWires | Draw::Grid => true,
            Draw::Clear | Draw::Rectangle | Draw::Hex | Draw::Circle | Draw::Text | Draw::Icon => {
                false
            }
        }
    }
}

#[derive(Debug, PartialEq)]
pub enum Statement<'input> {
    Info(Vec<Operand<'input>>),
    Call {
        span: Span,
        name: Named<'input>,
        arguments: Arguments<'input>,
        taking: Option<Taking<'input>>,
    },
    Join(Named<'input>, Vec<Operand<'input>>),
    Given {
        operation: (Span, crate::io::Builtin),
        values: Vec<Operand<'input>>,
        results: Vec<Taken<'input>>,
        block: Block<'input>,
    },
    Set {
        name: Named<'input>,
        operation: (Span, Operation),
        values: Vec<Operand<'input>>,
        then: Vec<Then<'input>>,
    },
    SetConversion {
        name: Named<'input>,
        conversion: (Span, Conversion),
        values: Vec<Operand<'input>>,
        then: Vec<Then<'input>>,
    },
    SetCall {
        name: Named<'input>,
        operation: (Span, Operation),
        call: Call<'input>,
        then: Vec<Then<'input>>,
    },
    Declare {
        name: Named<'input>,
        mutable: bool,
        initial: Initial<'input>,
        then: Vec<Then<'input>>,
    },
    DeclareArray {
        name: Named<'input>,
        mutable: bool,
        array: Span,
        columns: Vec<Column<'input>>,
        rows: Rows<'input>,
    },
    // A row, or one column of it when it is named.
    SetAt {
        name: Named<'input>,
        index: Operand<'input>,
        column: Option<Named<'input>>,
        operation: (Span, Operation),
        values: Vec<Operand<'input>>,
    },
    If {
        branches: Vec<(Condition<'input>, Block<'input>)>,
        otherwise: Option<Block<'input>>,
    },
    While(Span, Condition<'input>, Block<'input>),
    // for name from first to end di ... do, from inclusive and to exclusive.
    For {
        span: Span,
        name: Named<'input>,
        from: Operand<'input>,
        to: Operand<'input>,
        block: Block<'input>,
    },
    AwaitNextFrame(Span),
    Draw {
        span: Span,
        draw: Draw,
        align: Option<Align>,
        values: Vec<Operand<'input>>,
    },
    Camera3d {
        span: Span,
        values: Vec<Operand<'input>>,
        block: Block<'input>,
    },
    // gfx_mouse_capture when true, and gfx_mouse_release when false.
    MouseCapture(Span, bool),
    Pass,
    Return(Span, ExitStatus),
    ReturnValues(Span, Vec<Operand<'input>>),
}

#[derive(Debug, PartialEq)]
pub enum Rows<'input> {
    Literal(Vec<(Span, Vec<Operand<'input>>)>),
    Zeroed(Span, Operand<'input>),
}

#[derive(Debug, PartialEq)]
pub enum Results<'input> {
    Void,
    Named(Vec<Column<'input>>),
}

#[derive(Debug, PartialEq)]
pub enum Item<'input> {
    Mod(Span, String),
    // The range is from, inclusive, and to, exclusive, and is every i32 if
    // it is not given.
    Aka {
        public: Option<Span>,
        name: Named<'input>,
        range: Option<(Operand<'input>, Operand<'input>)>,
        except: Vec<Operand<'input>>,
    },
    // Known before the program runs, so it is computed from literals,
    // earlier constants and settings.
    Const {
        public: Option<Span>,
        name: Named<'input>,
        initial: Initial<'input>,
        then: Vec<Then<'input>>,
    },
    Def {
        public: Option<Span>,
        name: Named<'input>,
        parameters: Vec<Named<'input>>,
        globals: Vec<Named<'input>>,
        results: (Span, Results<'input>),
        block: Block<'input>,
    },
    Array {
        public: Option<Span>,
        name: Named<'input>,
        columns: Vec<Column<'input>>,
        rows: Vec<(Span, Vec<Operand<'input>>)>,
    },
    Section(Span, Section, Block<'input>, usize),
}

#[derive(Debug)]
pub struct File<'input> {
    pub items: Vec<Item<'input>>,
    pub end: usize,
}
