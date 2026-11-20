use std::mem;

use super::{Call, Callee, Comparison, Condition, Draw, Given, Operand, Program, Statement};
use crate::Diagnostic;
use crate::ast::ExitStatus;
use crate::gfx::{Scene, Shape, Window};
use crate::io::{FileIo, Io};
use crate::value::Value;

const CALL_DEPTH_MAX: usize = 10_000;
const DRAW_TEXT_LENGTH_MAX: usize = 1024;
const STEPS_PER_FRAME: usize = 100_000;
const FIRST_DELTA_TIME: f32 = 1.0 / 60.0;

enum Flow {
    Next,
    Exit(ExitStatus),
    Values(Vec<Value>),
    Closed,
}

impl Flow {
    fn is_next(&self) -> bool {
        matches!(self, Flow::Next)
    }
}

pub(super) fn run_text(program: &Program) -> Result<(String, ExitStatus), (String, Diagnostic)> {
    let mut io = FileIo;
    let mut execution = Execution::new(program, None, &mut io);
    let result = execution.run_text();
    match result {
        Ok(status) => Ok((execution.output, status)),
        Err(diagnostic) => Err((execution.output, diagnostic)),
    }
}

pub(super) fn run_window(
    program: &Program,
    window: &mut dyn Window,
    io: &mut dyn Io,
) -> Result<ExitStatus, String> {
    let mut execution = Execution::new(program, Some(window), io);
    execution.set_section_slot(program.delta_time_slot, Value::F32(FIRST_DELTA_TIME));
    let result = execution.run_window();
    let cleanup = execution.section(&program.sections.cleanup);
    execution.flush();
    let returned = match result {
        Ok(returned) => returned,
        Err(diagnostic) => return Err(program.located(&diagnostic)),
    };
    if let Err(diagnostic) = cleanup {
        return Err(program.located(&diagnostic));
    }
    match returned {
        Some(status) => Ok(status),
        None => Err(format!(
            "{}: the window was closed before the program returned",
            program.path()
        )),
    }
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
    if let Some(whole) = index.as_whole()
        && let Ok(row) = usize::try_from(whole)
        && row < rows
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

    fn run_text(&mut self) -> Result<ExitStatus, Diagnostic> {
        let program = self.program;
        if program.is_gfx {
            return Err(Diagnostic::new(0, "a gfx program runs in a window"));
        }
        self.block(&program.sections.global)?;
        match self.run_main()? {
            Some(status) => Ok(status),
            None => Ok(ExitStatus::Failure),
        }
    }

    fn run_window(&mut self) -> Result<Option<ExitStatus>, Diagnostic> {
        let program = self.program;
        self.block(&program.sections.global)?;
        self.block(&program.sections.init)?;
        if let Flow::Closed = self.next_frame()? {
            return Ok(None);
        }
        self.run_main()
    }

    fn run_main(&mut self) -> Result<Option<ExitStatus>, Diagnostic> {
        let program = self.program;
        let Some((statements, end)) = &program.sections.main else {
            return Err(Diagnostic::new(0, "a program needs a bosonoga_main block"));
        };
        self.in_main = true;
        let flow = self.block(statements);
        self.in_main = false;
        match flow? {
            Flow::Exit(status) => Ok(Some(status)),
            Flow::Closed => Ok(None),
            Flow::Next | Flow::Values(_) => {
                Err(Diagnostic::new(*end, "the program ended without a return"))
            }
        }
    }

    fn get<'v>(&'v self, operand: &'v Operand) -> &'v Value {
        match operand {
            Operand::Literal(value) => value,
            Operand::Slot(slot) => {
                if let Some(frame) = self.frames.last()
                    && let Some(value) = frame.get(*slot)
                {
                    return value;
                }
                &Value::Bool(false)
            }
        }
    }

    fn values(&self, operands: &[Operand]) -> Vec<Value> {
        let mut values = Vec::new();
        for operand in operands {
            values.push(self.get(operand).clone());
        }
        values
    }

    fn set(&mut self, slot: usize, value: Value) {
        if let Some(frame) = self.frames.last_mut()
            && let Some(place) = frame.get_mut(slot)
        {
            *place = value;
        }
    }

    fn set_section_slot(&mut self, slot: Option<usize>, value: Value) {
        let Some(at) = slot else {
            return;
        };
        if let Some(frame) = self.frames.first_mut()
            && let Some(place) = frame.get_mut(at)
        {
            *place = value;
        }
    }

    fn holds(&self, condition: &Condition) -> bool {
        for conjunction in condition {
            if self.all_hold(conjunction) {
                return true;
            }
        }
        false
    }

    fn all_hold(&self, comparisons: &[Comparison]) -> bool {
        for comparison in comparisons {
            let left = self.get(&comparison.left);
            let right = self.get(&comparison.right);
            if left.compare(comparison.compare, right) == comparison.negate {
                return false;
            }
        }
        true
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
        let mut shown = None;
        if let Some(window) = self.window.as_deref_mut() {
            shown = window.show(&self.scene);
        }
        let Some(input) = shown else {
            return Ok(Flow::Closed);
        };
        for key in input.keys {
            self.set_section_slot(program.key_slot, Value::InputKey(key));
            self.section(&program.sections.key_down)?;
        }
        self.set_section_slot(program.delta_time_slot, Value::F32(input.delta_time));
        Ok(Flow::Next)
    }

    fn call(&mut self, call: &Call) -> Result<Flow, Diagnostic> {
        let arguments = self.values(&call.arguments);
        match call.callee {
            Callee::Array(index) => self.read_array(index, &arguments, call.offset),
            Callee::VarArray(index) => self.read_var_array(index, &arguments, call.offset),
            Callee::Function(index) => self.call_function(index, arguments, call.offset),
        }
    }

    fn read_array(
        &self,
        index: usize,
        arguments: &[Value],
        offset: usize,
    ) -> Result<Flow, Diagnostic> {
        let Some(array) = self.program.arrays.get(index) else {
            return Ok(Flow::Next);
        };
        let Some(at) = arguments.first() else {
            return Ok(Flow::Next);
        };
        let row = row_start(&array.name, array.rows.len(), at, offset)?;
        let values = match array.rows.get(row) {
            Some(found) => found.clone(),
            None => Vec::new(),
        };
        Ok(Flow::Values(values))
    }

    fn read_var_array(
        &self,
        index: usize,
        arguments: &[Value],
        offset: usize,
    ) -> Result<Flow, Diagnostic> {
        let Some(array) = self.program.var_arrays.get(index) else {
            return Ok(Flow::Next);
        };
        let Some(at) = arguments.first() else {
            return Ok(Flow::Next);
        };
        let row = row_start(&array.name, array.rows, at, offset)?;
        let start = array.slot + row * array.columns;
        let mut values = Vec::new();
        if let Some(frame) = self.frames.last()
            && let Some(row_values) = frame.get(start..start + array.columns)
        {
            values = row_values.to_vec();
        }
        Ok(Flow::Values(values))
    }

    fn call_function(
        &mut self,
        index: usize,
        arguments: Vec<Value>,
        offset: usize,
    ) -> Result<Flow, Diagnostic> {
        let program = self.program;
        let Some(function) = program.functions.get(index) else {
            return Ok(Flow::Next);
        };
        if self.frames.len() > CALL_DEPTH_MAX {
            return Err(Diagnostic::new(
                offset,
                format!("calls are nested more than {CALL_DEPTH_MAX} deep"),
            ));
        }
        let mut frame = arguments;
        frame.resize(function.slot_count, Value::Bool(false));
        self.frames.push(frame);
        let returned = self.block(&function.statements);
        self.frames.pop();
        let flow = returned?;
        if flow.is_next() && function.returns_values {
            return Err(Diagnostic::new(
                function.end,
                format!("the function '{}' ended without a return", function.name),
            ));
        }
        Ok(flow)
    }

    fn real(&self, operand: &Operand) -> f32 {
        self.get(operand).as_f32()
    }

    fn color(&self, operand: &Operand) -> u32 {
        self.get(operand).as_color()
    }

    fn draw(&mut self, draw: &Draw, offset: usize) -> Result<(), Diagnostic> {
        let shape = match draw {
            Draw::Clear(value) => {
                self.scene.clear = self.color(value);
                return Ok(());
            }
            Draw::Rectangle([x, y, width, height, fill]) => Shape::Rectangle {
                x: self.real(x),
                y: self.real(y),
                width: self.real(width),
                height: self.real(height),
                color: self.color(fill),
            },
            Draw::Hex([x, y, size, fill]) => {
                let radius = self.real(size);
                if radius <= 0.0 {
                    return Ok(());
                }
                Shape::Hex {
                    x: self.real(x),
                    y: self.real(y),
                    radius,
                    color: self.color(fill),
                }
            }
            Draw::Circle([x, y, size, fill]) => {
                let radius = self.real(size);
                if radius <= 0.0 {
                    return Ok(());
                }
                Shape::Circle {
                    x: self.real(x),
                    y: self.real(y),
                    radius,
                    color: self.color(fill),
                }
            }
            Draw::Icon([x, y, image, angle, fill]) => Shape::Icon {
                x: self.real(x),
                y: self.real(y),
                icon: asset_index(self.get(image)),
                angle: self.real(angle),
                color: self.color(fill),
            },
            Draw::Text {
                align,
                values: [x, y, typeface, size, fill],
                parts,
            } => {
                let text = self.draw_text(parts, offset)?;
                let height = self.real(size);
                if height <= 0.0 {
                    return Ok(());
                }
                Shape::Text {
                    x: self.real(x),
                    y: self.real(y),
                    font: asset_index(self.get(typeface)),
                    size: height,
                    color: self.color(fill),
                    align: *align,
                    text,
                }
            }
        };
        self.scene.shapes.push(shape);
        Ok(())
    }

    fn draw_text(&self, parts: &[Operand], offset: usize) -> Result<String, Diagnostic> {
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
        Ok(text)
    }

    fn write_array(
        &mut self,
        array: usize,
        index: &Operand,
        operands: &[Operand],
        offset: usize,
    ) -> Result<(), Diagnostic> {
        let Some(target) = self.program.var_arrays.get(array) else {
            return Ok(());
        };
        let row = row_start(&target.name, target.rows, self.get(index), offset)?;
        let start = target.slot + row * target.columns;
        let values = self.values(operands);
        for (column, value) in values.into_iter().enumerate() {
            self.set(start + column, value);
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
            if !stepped.is_next() {
                return Ok(stepped);
            }
            let flow = self.statement(statement)?;
            if !flow.is_next() {
                return Ok(flow);
            }
        }
        Ok(Flow::Next)
    }

    fn given(&mut self, given: &Given) -> Result<Flow, Diagnostic> {
        let values = self.values(&given.arguments);
        let directory = given.directory.as_deref();
        let mut results = given.builtin.evaluate(directory, &values, self.io);
        for (index, slot) in &given.slots {
            if let Some(value) = results.get_mut(*index) {
                let taken = mem::replace(value, Value::Bool(false));
                self.set(*slot, taken);
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
            if !flow.is_next() {
                return Ok(flow);
            }
        }
        Ok(Flow::Next)
    }

    fn choose(
        &mut self,
        branches: &[(Condition, Vec<Statement>)],
        otherwise: Option<&Vec<Statement>>,
    ) -> Result<Flow, Diagnostic> {
        for (condition, block) in branches {
            if self.holds(condition) {
                return self.block(block);
            }
        }
        match otherwise {
            Some(block) => self.block(block),
            None => Ok(Flow::Next),
        }
    }

    fn info(&mut self, operands: &[Operand]) {
        for operand in operands {
            let shown = self.get(operand).to_string();
            self.output.push_str(&shown);
        }
        self.output.push('\n');
    }

    fn join(&mut self, slot: usize, operands: &[Operand]) {
        let mut joined = String::new();
        for operand in operands {
            joined.push_str(&self.get(operand).to_string());
        }
        self.set(slot, Value::Str(joined));
    }

    fn assign(&mut self, slot: usize, initial: &Operand, shift_or: &[Operand]) {
        let mut value = self.get(initial).clone();
        for flags in shift_or {
            if let Value::Flags(bits) = &mut value
                && let Value::Flags(more) = self.get(flags)
            {
                *bits |= more;
            }
        }
        self.set(slot, value);
    }

    fn init_array(&mut self, slot: usize, values: &[Value], repeat: usize) {
        for row in 0..repeat {
            let start = slot + row * values.len();
            for (column, value) in values.iter().enumerate() {
                self.set(start + column, value.clone());
            }
        }
    }

    fn statement(&mut self, statement: &Statement) -> Result<Flow, Diagnostic> {
        match statement {
            Statement::Info(operands) => self.info(operands),
            Statement::Join(slot, operands) => self.join(*slot, operands),
            Statement::Given(given) => return self.given(given),
            Statement::Calculate {
                slot,
                operation,
                values,
                offset,
            } => {
                let mut operands = Vec::new();
                for value in values {
                    operands.push(self.get(value));
                }
                let result = match operation.evaluate(&operands) {
                    Ok(result) => result,
                    Err(message) => return Err(Diagnostic::new(*offset, message)),
                };
                self.set(*slot, result);
            }
            Statement::Convert {
                slot,
                conversion,
                left,
                right,
                offset,
            } => {
                let mut right_value = None;
                if let Some(operand) = right {
                    right_value = Some(self.get(operand));
                }
                let result = match conversion.evaluate(self.get(left), right_value) {
                    Ok(result) => result,
                    Err(message) => return Err(Diagnostic::new(*offset, message)),
                };
                self.set(*slot, result);
            }
            Statement::Assign {
                slot,
                initial,
                shift_or,
            } => self.assign(*slot, initial, shift_or),
            Statement::Call { slot, call } => {
                let flow = self.call(call)?;
                match flow {
                    Flow::Values(mut values) => {
                        if !values.is_empty() {
                            let first = values.remove(0);
                            self.set(*slot, first);
                        }
                    }
                    Flow::Exit(_) | Flow::Closed => return Ok(flow),
                    Flow::Next => {}
                }
            }
            Statement::ArrayInit {
                slot,
                values,
                repeat,
            } => self.init_array(*slot, values, *repeat),
            Statement::ArrayWrite {
                array,
                index,
                values,
                offset,
            } => self.write_array(*array, index, values, *offset)?,
            Statement::If {
                branches,
                otherwise,
            } => return self.choose(branches, otherwise.as_ref()),
            Statement::While(condition, block) => return self.repeat(condition, block),
            Statement::AwaitNextFrame => return self.next_frame(),
            Statement::Draw { draw, offset } => self.draw(draw, *offset)?,
            Statement::Return(status) => return Ok(Flow::Exit(*status)),
            Statement::ReturnValues(operands) => return Ok(Flow::Values(self.values(operands))),
        }
        Ok(Flow::Next)
    }
}
