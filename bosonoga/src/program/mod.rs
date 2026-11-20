mod execution;

use crate::Diagnostic;
use crate::ast::ExitStatus;
use crate::gfx::{Align, Window};
use crate::io::{Builtin, Io};
use crate::settings::Asset;
use crate::sources::Sources;
use crate::value::{Compare, Conversion, Operation, Value};

#[derive(Debug)]
pub(crate) enum Operand {
    Literal(Value),
    Slot(usize),
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

#[derive(Debug)]
pub(crate) struct Call {
    pub(crate) callee: Callee,
    pub(crate) arguments: Vec<Operand>,
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
}

#[derive(Debug)]
pub(crate) struct Given {
    pub(crate) builtin: Builtin,
    pub(crate) directory: Option<std::path::PathBuf>,
    pub(crate) arguments: Vec<Operand>,
    pub(crate) slots: Vec<(usize, usize)>,
    pub(crate) block: Vec<Statement>,
}

#[derive(Debug)]
pub(crate) enum Statement {
    Info(Vec<Operand>),
    Join(usize, Vec<Operand>),
    Given(Given),
    Calculate {
        slot: usize,
        operation: Operation,
        values: Vec<Operand>,
        offset: usize,
    },
    Convert {
        slot: usize,
        conversion: Conversion,
        left: Operand,
        right: Option<Operand>,
        offset: usize,
    },
    Assign {
        slot: usize,
        initial: Operand,
        shift_or: Vec<Operand>,
    },
    Call {
        slot: usize,
        call: Call,
    },
    ArrayInit {
        slot: usize,
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
    While(Condition, Vec<Statement>),
    AwaitNextFrame,
    Draw {
        draw: Draw,
        offset: usize,
    },
    Return(ExitStatus),
    ReturnValues(Vec<Operand>),
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
    pub(crate) slot: usize,
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
    pub(crate) cleanup: Vec<Statement>,
}

#[derive(Debug, Default)]
pub struct Program {
    pub(crate) sources: Sources,
    pub(crate) sections: Sections,
    pub(crate) is_gfx: bool,
    pub(crate) slot_count: usize,
    pub(crate) key_slot: Option<usize>,
    pub(crate) delta_time_slot: Option<usize>,
    pub(crate) functions: Vec<Function>,
    pub(crate) arrays: Vec<Array>,
    pub(crate) var_arrays: Vec<VarArray>,
    pub(crate) fonts: Vec<(String, Asset)>,
    pub(crate) icons: Vec<(String, Asset)>,
    pub(crate) background: u32,
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
