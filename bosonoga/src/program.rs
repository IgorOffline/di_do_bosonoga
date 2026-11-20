use std::mem;

use crate::Diagnostic;
use crate::ast::ExitStatus;
use crate::gfx::{Align, Input, Scene, Shape, Window};
use crate::io::{Builtin, FileIo, Io};
use crate::settings::Asset;
use crate::sources::Sources;
use crate::value::{Compare, Conversion, Operation, Value};

const CALL_DEPTH_MAX: usize = 10_000;
const DRAW_TEXT_LENGTH_MAX: usize = 1024;
const STEPS_PER_FRAME: usize = 100_000;
const FIRST_DELTA_TIME: f32 = 1.0 / 60.0;

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

fn asset_index(value: &Value) -> usize {
    match value {
        Value::Font { index, .. } | Value::Icon { index, .. } => *index,
        Value::I32(_)
        | Value::I64(_)
        | Value::U32(_)
        | Value::U64(_)
        | Value::F32(_)
        | Value::F64(_)
        | Value::Flags(_)
        | Value::Color(_)
        | Value::Str(_)
        | Value::Bool(_)
        | Value::InputKey(_) => 0,
    }
}

fn row_start(name: &str, rows: usize, index: &Value, offset: usize) -> Result<usize, Diagnostic> {
    if let Some(row) = index
        .as_whole()
        .and_then(|at| usize::try_from(at).ok())
        .filter(|row| *row < rows)
    {
        return Ok(row);
    }
    let message = if rows == 1 {
        format!("'{name}' has only row 0, not {index}")
    } else {
        format!(
            "'{name}' has rows 0 to {}, not {index}",
            rows.saturating_sub(1)
        )
    };
    Err(Diagnostic::new(offset, message))
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

enum Flow {
    Next,
    Exit(ExitStatus),
    Values(Vec<Value>),
    Closed,
}

struct Execution<'a> {
    program: &'a Program,
    frames: Vec<Vec<Value>>,
    output: String,
    window: Option<&'a mut dyn Window>,
    scene: Scene,
    io: &'a mut dyn Io,
    in_main: bool,
    steps: usize,
}

impl<'a> Execution<'a> {
    fn new(program: &'a Program, window: Option<&'a mut dyn Window>, io: &'a mut dyn Io) -> Self {
        Execution {
            program,
            frames: vec![vec![Value::Bool(false); program.slot_count]],
            output: String::new(),
            window,
            scene: Scene::default(),
            io,
            in_main: false,
            steps: 0,
        }
    }

    fn get<'v>(&'v self, operand: &'v Operand) -> &'v Value {
        match operand {
            Operand::Literal(value) => value,
            Operand::Slot(slot) => self
                .frames
                .last()
                .and_then(|frame| frame.get(*slot))
                .unwrap_or(&Value::Bool(false)),
        }
    }

    fn values(&self, operands: &[Operand]) -> Vec<Value> {
        operands
            .iter()
            .map(|operand| self.get(operand).clone())
            .collect()
    }

    fn set(&mut self, slot: usize, value: Value) {
        if let Some(place) = self.frames.last_mut().and_then(|frame| frame.get_mut(slot)) {
            *place = value;
        }
    }

    fn set_section_slot(&mut self, slot: Option<usize>, value: Value) {
        if let Some(place) = slot.and_then(|at| self.frames.first_mut()?.get_mut(at)) {
            *place = value;
        }
    }

    fn holds(&self, condition: &Condition) -> bool {
        condition.iter().any(|conjunction| {
            conjunction.iter().all(|comparison| {
                self.get(&comparison.left)
                    .compare(comparison.compare, self.get(&comparison.right))
                    != comparison.negate
            })
        })
    }

    fn section(&mut self, statements: &[Statement]) -> Result<Flow, Diagnostic> {
        let calls = self.frames.split_off(1);
        let in_main = mem::replace(&mut self.in_main, false);
        let flow = self.block(statements);
        self.in_main = in_main;
        self.frames.extend(calls);
        flow
    }

    fn flush(&mut self) {
        if let Some(window) = self.window.as_deref_mut()
            && !self.output.is_empty()
        {
            window.info(&self.output);
            self.output.clear();
        }
    }

    fn next_frame(&mut self) -> Result<Flow, Diagnostic> {
        let program = self.program;
        self.steps = 0;
        self.scene = Scene {
            clear: program.background,
            shapes: Vec::new(),
        };
        self.section(&program.sections.frame)?;
        self.flush();
        let Some(Input { keys, delta_time }) = self
            .window
            .as_deref_mut()
            .and_then(|window| window.show(&self.scene))
        else {
            return Ok(Flow::Closed);
        };
        for key in keys {
            self.set_section_slot(program.key_slot, Value::InputKey(key));
            self.section(&program.sections.key_down)?;
        }
        self.set_section_slot(program.delta_time_slot, Value::F32(delta_time));
        Ok(Flow::Next)
    }

    fn call(&mut self, call: &Call) -> Result<Flow, Diagnostic> {
        let program = self.program;
        let arguments = self.values(&call.arguments);
        match call.callee {
            Callee::Array(index) => {
                let (Some(array), Some(at)) = (program.arrays.get(index), arguments.first()) else {
                    return Ok(Flow::Next);
                };
                let row = row_start(&array.name, array.rows.len(), at, call.offset)?;
                Ok(Flow::Values(
                    array.rows.get(row).cloned().unwrap_or_default(),
                ))
            }
            Callee::VarArray(index) => {
                let (Some(array), Some(at)) = (program.var_arrays.get(index), arguments.first())
                else {
                    return Ok(Flow::Next);
                };
                let row = row_start(&array.name, array.rows, at, call.offset)?;
                let start = array.slot + row * array.columns;
                Ok(Flow::Values(
                    self.frames
                        .last()
                        .and_then(|frame| frame.get(start..start + array.columns))
                        .map_or_else(Vec::new, <[Value]>::to_vec),
                ))
            }
            Callee::Function(index) => {
                let Some(function) = program.functions.get(index) else {
                    return Ok(Flow::Next);
                };
                if self.frames.len() > CALL_DEPTH_MAX {
                    return Err(Diagnostic::new(
                        call.offset,
                        format!("calls are nested more than {CALL_DEPTH_MAX} deep"),
                    ));
                }
                let mut frame = arguments;
                frame.resize(function.slot_count, Value::Bool(false));
                self.frames.push(frame);
                let returned = self.block(&function.statements);
                self.frames.pop();
                match returned? {
                    Flow::Next if function.returns_values => Err(Diagnostic::new(
                        function.end,
                        format!("the function '{}' ended without a return", function.name),
                    )),
                    Flow::Next => Ok(Flow::Next),
                    flow @ (Flow::Values(_) | Flow::Exit(_) | Flow::Closed) => Ok(flow),
                }
            }
        }
    }

    fn draw(&mut self, draw: &Draw, offset: usize) -> Result<(), Diagnostic> {
        let real = |execution: &Self, operand: &Operand| execution.get(operand).as_f32();
        let color = |execution: &Self, operand: &Operand| execution.get(operand).as_color();
        let shape = match draw {
            Draw::Clear(value) => {
                self.scene.clear = color(self, value);
                return Ok(());
            }
            Draw::Rectangle([x, y, width, height, fill]) => Shape::Rectangle {
                x: real(self, x),
                y: real(self, y),
                width: real(self, width),
                height: real(self, height),
                color: color(self, fill),
            },
            Draw::Hex([x, y, size, fill]) | Draw::Circle([x, y, size, fill]) => {
                let radius = real(self, size);
                if radius <= 0.0 {
                    return Ok(());
                }
                let (left, top, shade) = (real(self, x), real(self, y), color(self, fill));
                if matches!(draw, Draw::Hex(_)) {
                    Shape::Hex {
                        x: left,
                        y: top,
                        radius,
                        color: shade,
                    }
                } else {
                    Shape::Circle {
                        x: left,
                        y: top,
                        radius,
                        color: shade,
                    }
                }
            }
            Draw::Icon([x, y, image, angle, fill]) => Shape::Icon {
                x: real(self, x),
                y: real(self, y),
                icon: asset_index(self.get(image)),
                angle: real(self, angle),
                color: color(self, fill),
            },
            Draw::Text {
                align,
                values: [x, y, typeface, size, fill],
                parts,
            } => {
                let mut text = String::new();
                for part in parts {
                    text.push_str(&self.get(part).to_string());
                    if text.len() > DRAW_TEXT_LENGTH_MAX {
                        return Err(Diagnostic::new(
                            offset,
                            format!("gfx_draw_text is longer than {DRAW_TEXT_LENGTH_MAX} bytes"),
                        ));
                    }
                }
                let height = real(self, size);
                if height <= 0.0 {
                    return Ok(());
                }
                let font = asset_index(self.get(typeface));
                Shape::Text {
                    x: real(self, x),
                    y: real(self, y),
                    font,
                    size: height,
                    color: color(self, fill),
                    align: *align,
                    text,
                }
            }
        };
        self.scene.shapes.push(shape);
        Ok(())
    }

    fn write_array(
        &mut self,
        array: usize,
        index: &Operand,
        values: &[Operand],
        offset: usize,
    ) -> Result<(), Diagnostic> {
        if let Some(target) = self.program.var_arrays.get(array) {
            let row = row_start(&target.name, target.rows, self.get(index), offset)?;
            let start = target.slot + row * target.columns;
            for (column, value) in self.values(values).into_iter().enumerate() {
                self.set(start + column, value);
            }
        }
        Ok(())
    }

    fn step(&mut self) -> Result<Flow, Diagnostic> {
        if self.in_main && self.window.is_some() {
            self.steps += 1;
            if self.steps >= STEPS_PER_FRAME {
                return self.next_frame();
            }
        }
        Ok(Flow::Next)
    }

    fn block(&mut self, statements: &[Statement]) -> Result<Flow, Diagnostic> {
        for statement in statements {
            let stepped = self.step()?;
            if !matches!(stepped, Flow::Next) {
                return Ok(stepped);
            }
            let flow = self.statement(statement)?;
            if !matches!(flow, Flow::Next) {
                return Ok(flow);
            }
        }
        Ok(Flow::Next)
    }

    fn given(&mut self, given: &Given) -> Result<Flow, Diagnostic> {
        let values = self.values(&given.arguments);
        let mut results = given
            .builtin
            .evaluate(given.directory.as_deref(), &values, self.io);
        for (index, slot) in &given.slots {
            if let Some(value) = results.get_mut(*index) {
                self.set(*slot, mem::replace(value, Value::Bool(false)));
            }
        }
        let flow = self.block(&given.block);
        for (_, slot) in &given.slots {
            self.set(*slot, Value::Bool(false));
        }
        flow
    }

    fn repeat(&mut self, condition: &Condition, block: &[Statement]) -> Result<Flow, Diagnostic> {
        while self.holds(condition) {
            let flow = self.block(block)?;
            if !matches!(flow, Flow::Next) {
                return Ok(flow);
            }
        }
        Ok(Flow::Next)
    }

    fn statement(&mut self, statement: &Statement) -> Result<Flow, Diagnostic> {
        match statement {
            Statement::Info(values) => {
                for value in values {
                    let shown = self.get(value).to_string();
                    self.output.push_str(&shown);
                }
                self.output.push('\n');
            }
            Statement::Join(slot, values) => {
                let joined = values
                    .iter()
                    .map(|value| self.get(value).to_string())
                    .collect();
                self.set(*slot, Value::Str(joined));
            }
            Statement::Given(given) => return self.given(given),
            Statement::Calculate {
                slot,
                operation,
                values,
                offset,
            } => {
                let operands: Vec<&Value> = values.iter().map(|value| self.get(value)).collect();
                let result = operation
                    .evaluate(&operands)
                    .map_err(|message| Diagnostic::new(*offset, message))?;
                self.set(*slot, result);
            }
            Statement::Convert {
                slot,
                conversion,
                left,
                right,
                offset,
            } => {
                let result = conversion
                    .evaluate(self.get(left), right.as_ref().map(|value| self.get(value)))
                    .map_err(|message| Diagnostic::new(*offset, message))?;
                self.set(*slot, result);
            }
            Statement::Assign {
                slot,
                initial,
                shift_or,
            } => {
                let mut value = self.get(initial).clone();
                for flags in shift_or {
                    if let (Value::Flags(bits), Value::Flags(more)) = (&mut value, self.get(flags))
                    {
                        *bits |= more;
                    }
                }
                self.set(*slot, value);
            }
            Statement::Call { slot, call } => match self.call(call)? {
                Flow::Values(values) => {
                    if let Some(first) = values.into_iter().next() {
                        self.set(*slot, first);
                    }
                }
                flow @ (Flow::Exit(_) | Flow::Closed) => return Ok(flow),
                Flow::Next => {}
            },
            Statement::ArrayInit {
                slot,
                values,
                repeat,
            } => {
                for row in 0..*repeat {
                    for (column, value) in values.iter().enumerate() {
                        self.set(slot + row * values.len() + column, value.clone());
                    }
                }
            }
            Statement::ArrayWrite {
                array,
                index,
                values,
                offset,
            } => self.write_array(*array, index, values, *offset)?,
            Statement::If {
                branches,
                otherwise,
            } => {
                let taken = branches
                    .iter()
                    .find(|(condition, _)| self.holds(condition))
                    .map(|(_, block)| block)
                    .or(otherwise.as_ref());
                if let Some(block) = taken {
                    return self.block(block);
                }
            }
            Statement::While(condition, block) => return self.repeat(condition, block),
            Statement::AwaitNextFrame => return self.next_frame(),
            Statement::Draw { draw, offset } => self.draw(draw, *offset)?,
            Statement::Return(status) => return Ok(Flow::Exit(*status)),
            Statement::ReturnValues(values) => return Ok(Flow::Values(self.values(values))),
        }
        Ok(Flow::Next)
    }
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
        let mut io = FileIo;
        let mut execution = Execution::new(self, None, &mut io);
        let flow = if self.is_gfx {
            Err(Diagnostic::new(0, "a gfx program runs in a window"))
        } else {
            execution
                .block(&self.sections.global)
                .and_then(|_| self.run_main(&mut execution))
                .map(|status| status.unwrap_or(ExitStatus::Failure))
        };
        match flow {
            Ok(status) => Ok((execution.output, status)),
            Err(diagnostic) => Err((execution.output, diagnostic)),
        }
    }

    fn run_main(&self, execution: &mut Execution<'_>) -> Result<Option<ExitStatus>, Diagnostic> {
        let Some((statements, end)) = &self.sections.main else {
            return Err(Diagnostic::new(0, "a program needs a bosonoga_main block"));
        };
        execution.in_main = true;
        let flow = execution.block(statements);
        execution.in_main = false;
        match flow? {
            Flow::Exit(status) => Ok(Some(status)),
            Flow::Closed => Ok(None),
            Flow::Next | Flow::Values(_) => {
                Err(Diagnostic::new(*end, "the program ended without a return"))
            }
        }
    }

    pub(crate) fn run_gfx(
        &self,
        window: &mut dyn Window,
        io: &mut dyn Io,
    ) -> Result<ExitStatus, String> {
        let mut execution = Execution::new(self, Some(window), io);
        execution.set_section_slot(self.delta_time_slot, Value::F32(FIRST_DELTA_TIME));
        let result = self.run_window(&mut execution);
        let cleanup = execution.section(&self.sections.cleanup);
        execution.flush();
        let status = result.map_err(|diagnostic| self.located(&diagnostic))?;
        cleanup.map_err(|diagnostic| self.located(&diagnostic))?;
        status.ok_or_else(|| {
            format!(
                "{}: the window was closed before the program returned",
                self.path()
            )
        })
    }

    fn run_window(&self, execution: &mut Execution<'_>) -> Result<Option<ExitStatus>, Diagnostic> {
        execution.block(&self.sections.global)?;
        execution.block(&self.sections.init)?;
        if matches!(execution.next_frame()?, Flow::Closed) {
            return Ok(None);
        }
        self.run_main(execution)
    }
}
