mod execution;
mod heap;
mod space;

use std::fmt::{self, Display, Formatter};

use crate::Diagnostic;
use crate::ast::ExitStatus;
use crate::gfx::{Align, Window};
use crate::io::{Builtin, Io};
use crate::setting::Asset;
use crate::source::Sources;
use crate::value::{Compare, Conversion, Operation, Value};

// Where a variable lives: in the frame of the running function or section,
// or, for a variable of bosonoga_global, in the first frame, which a def
// reaches with from_global.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Slot {
    Local(usize),
    Global(usize),
}

impl Slot {
    pub(crate) fn new(index: usize, global: bool) -> Self {
        if global {
            Slot::Global(index)
        } else {
            Slot::Local(index)
        }
    }

    pub(crate) fn is_global(self) -> bool {
        matches!(self, Slot::Global(_))
    }

    pub(crate) fn index(self) -> usize {
        match self {
            Slot::Local(index) | Slot::Global(index) => index,
        }
    }

    // The slot that is offset after this one, in the same frame.
    pub(crate) fn after(self, offset: usize) -> Self {
        match self {
            Slot::Local(index) => Slot::Local(index + offset),
            Slot::Global(index) => Slot::Global(index + offset),
        }
    }
}

#[derive(Clone, Debug)]
pub(crate) enum Operand {
    Literal(Value),
    Slot(Slot),
}

#[derive(Debug)]
pub(crate) struct Comparison {
    pub(crate) negate: bool,
    pub(crate) left: Operand,
    pub(crate) compare: Compare,
    pub(crate) right: Operand,
}

pub(crate) type Condition = Vec<Vec<Comparison>>;

#[derive(Debug)]
pub(crate) enum Callee {
    Function(usize),
    Array(usize),
    VarArray(usize),
}

// The values of an aka_i32: those of the range, which is from, inclusive, and
// to, exclusive, or else every i32, but not those of except, which increase.
#[derive(Debug)]
pub(crate) struct Aka {
    pub(crate) name: String,
    pub(crate) range: Option<(i32, i32)>,
    pub(crate) except: Vec<i32>,
}

impl Aka {
    pub(crate) fn has(&self, value: i32) -> bool {
        if let Some((from, to)) = self.range
            && (value < from || value >= to)
        {
            return false;
        }
        self.except.binary_search(&value).is_err()
    }

    // Why a call of a function cannot be given a value, or nothing if it can.
    pub(crate) fn refusal(&self, function: &str, value: i32) -> Option<String> {
        if self.has(value) {
            return None;
        }
        Some(format!(
            "'{function}' needs {}, an i32{self}, but is given {value}_i32",
            self.name
        ))
    }
}

// As it is written, as in from 0_i32 to 6_i32 except 3_i32 4_i32.
impl Display for Aka {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> fmt::Result {
        if let Some((from, to)) = self.range {
            write!(formatter, " from {from}_i32 to {to}_i32")?;
        }
        if !self.except.is_empty() {
            write!(formatter, " except")?;
        }
        for value in &self.except {
            write!(formatter, " {value}_i32")?;
        }
        Ok(())
    }
}

// An argument of a call that is not known before the program runs, so the
// call checks that it is a value of the aka_i32 of its parameter.
#[derive(Debug)]
pub(crate) struct AkaCheck {
    pub(crate) argument: usize,
    pub(crate) aka: usize,
    pub(crate) offset: usize,
}

#[derive(Debug)]
pub(crate) struct Call {
    pub(crate) callee: Callee,
    pub(crate) arguments: Vec<Operand>,
    pub(crate) aka_checks: Vec<AkaCheck>,
    pub(crate) offset: usize,
}

#[derive(Debug)]
pub(crate) enum Draw {
    Clear(Operand),
    Rectangle([Operand; 5]),
    Hex([Operand; 4]),
    Circle([Operand; 4]),
    Icon([Operand; 5]),
    Text {
        align: Align,
        values: [Operand; 5],
        parts: Vec<Operand>,
    },
    Cube([Operand; 7]),
    CubeWires([Operand; 7]),
    Grid([Operand; 4]),
}

#[derive(Debug)]
pub(crate) struct Given {
    pub(crate) builtin: Builtin,
    pub(crate) directory: Option<std::path::PathBuf>,
    pub(crate) arguments: Vec<Operand>,
    pub(crate) slots: Vec<(usize, Slot)>,
    pub(crate) block: Vec<Statement>,
}

// A call statement: the values it takes by (index, slot), then its block.
// A function of no values takes none and has no block.
#[derive(Debug)]
pub(crate) struct Invoke {
    pub(crate) call: Call,
    pub(crate) slots: Vec<(usize, Slot)>,
    pub(crate) block: Vec<Statement>,
}

#[derive(Debug)]
pub(crate) enum Statement {
    Info(Vec<Operand>),
    Invoke(Invoke),
    Join(Slot, Vec<Operand>),
    Given(Given),
    Calculate {
        slot: Slot,
        operation: Operation,
        values: Vec<Operand>,
        offset: usize,
    },
    Convert {
        slot: Slot,
        conversion: Conversion,
        left: Operand,
        right: Option<Operand>,
        offset: usize,
    },
    Assign {
        slot: Slot,
        initial: Operand,
        shift_or: Vec<Operand>,
    },
    Call {
        slot: Slot,
        call: Call,
    },
    ArrayInit {
        slot: Slot,
        values: Vec<Value>,
        repeat: usize,
    },
    ArrayWrite {
        array: usize,
        index: Operand,
        values: Vec<Operand>,
        offset: usize,
    },
    If {
        branches: Vec<(Condition, Vec<Statement>)>,
        otherwise: Option<Vec<Statement>>,
    },
    While {
        condition: Condition,
        block: Vec<Statement>,
        offset: usize,
    },
    AwaitNextFrame,
    Draw {
        draw: Draw,
        offset: usize,
    },
    Camera3d {
        camera: [Operand; 10],
        block: Vec<Statement>,
        offset: usize,
    },
    // gfx_mouse_capture when true, and gfx_mouse_release when false.
    MouseCapture {
        capture: bool,
        offset: usize,
    },
    Return(ExitStatus),
    ReturnValues(Vec<Operand>),
    // Copies the value of a present optional into the slot that
    // if some ... as names, first in the block of that if.
    Unwrap {
        slot: Slot,
        optional: Operand,
    },
}

#[derive(Debug)]
pub(crate) struct Function {
    pub(crate) name: String,
    pub(crate) slot_count: usize,
    pub(crate) statements: Vec<Statement>,
    pub(crate) returns_values: bool,
    pub(crate) end: usize,
}

#[derive(Debug)]
pub(crate) struct Array {
    pub(crate) name: String,
    pub(crate) rows: Vec<Vec<Value>>,
}

#[derive(Debug)]
pub(crate) struct VarArray {
    pub(crate) name: String,
    pub(crate) slot: Slot,
    pub(crate) columns: usize,
    pub(crate) rows: usize,
}

#[derive(Debug, Default)]
pub(crate) struct Sections {
    pub(crate) global: Vec<Statement>,
    pub(crate) main: Option<(Vec<Statement>, usize)>,
    pub(crate) init: Vec<Statement>,
    pub(crate) frame: Vec<Statement>,
    pub(crate) key_down: Vec<Statement>,
    pub(crate) key_up: Vec<Statement>,
    pub(crate) cleanup: Vec<Statement>,
}

#[derive(Debug, Default)]
pub struct Program {
    pub(crate) sources: Sources,
    pub(crate) sections: Sections,
    pub(crate) is_gfx: bool,
    pub(crate) slot_count: usize,
    // Of gfx_key, for gfx_key_down and gfx_key_up.
    pub(crate) key_slot: Option<Slot>,
    pub(crate) delta_time_slot: Option<Slot>,
    // Of gfx_heap_bytes, gfx_mouse_move_x, gfx_mouse_move_y and
    // gfx_mouse_captured, each only when the program reads it.
    pub(crate) heap_slot: Option<Slot>,
    pub(crate) mouse_move_x_slot: Option<Slot>,
    pub(crate) mouse_move_y_slot: Option<Slot>,
    pub(crate) mouse_captured_slot: Option<Slot>,
    pub(crate) akas: Vec<Aka>,
    pub(crate) functions: Vec<Function>,
    pub(crate) arrays: Vec<Array>,
    pub(crate) var_arrays: Vec<VarArray>,
    pub(crate) fonts: Vec<(String, Asset)>,
    pub(crate) icons: Vec<(String, Asset)>,
    pub(crate) background: u32,
    // How the pixels the mouse moved become its move.
    pub(crate) mouse: crate::setting::Mouse,
}

impl Program {
    #[must_use]
    pub fn is_gfx(&self) -> bool {
        self.is_gfx
    }

    #[must_use]
    pub fn path(&self) -> &str {
        self.sources.path()
    }

    #[must_use]
    pub fn fonts(&self) -> &[(String, Asset)] {
        &self.fonts
    }

    #[must_use]
    pub fn icons(&self) -> &[(String, Asset)] {
        &self.icons
    }

    #[must_use]
    pub fn located(&self, diagnostic: &Diagnostic) -> String {
        self.sources.located(diagnostic)
    }

    pub fn run(&self) -> Result<(String, ExitStatus), (String, Diagnostic)> {
        execution::run_text(self)
    }

    pub(crate) fn run_gfx(
        &self,
        window: &mut dyn Window,
        io: &mut dyn Io,
    ) -> Result<ExitStatus, String> {
        execution::run_window(self, window, io)
    }
}
