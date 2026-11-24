use std::mem;

use super::space;
use super::{
    Call, Callee, Comparison, Condition, Draw, Given, Invoke, Operand, Program, Slot, Statement,
};
use crate::Diagnostic;
use crate::ast::ExitStatus;
use crate::gfx::{KeyEvent, Scene, Shape, Window};
use crate::io::{FileIo, Io};
use crate::value::Value;

const CALL_DEPTH_MAX: usize = 10_000;
const DRAW_TEXT_LENGTH_MAX: usize = 1024;
// The most statements that a section of a gfx program runs before the next
// frame, which gfx_main reaches with gfx_await_next_frame.
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
    execution.set_section_slot(program.mouse_move_x_slot, Value::F32(0.0));
    execution.set_section_slot(program.mouse_move_y_slot, Value::F32(0.0));
    execution.set_mouse_captured(false);
    let result = execution.run_window();
    let cleanup = execution.section("gfx_cleanup", &program.sections.cleanup);
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
        | Value::InputKey(_)
        | Value::Optional { .. } => 0,
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
    // Whether gfx_camera_3d ran in this frame, which it does at most once.
    camera_3d: bool,
    // Whether the mouse is captured, as the program asked or as the window
    // last said.
    mouse_captured: bool,
    io: &'a mut dyn Io,
    // The section that runs, its statements since its start or since
    // gfx_await_next_frame, and the while or call that a step error names.
    running: &'static str,
    steps: usize,
    at: usize,
}

impl<'a> Execution<'a> {
    fn new(program: &'a Program, window: Option<&'a mut dyn Window>, io: &'a mut dyn Io) -> Self {
        Execution {
            program,
            frames: vec![vec![Value::Bool(false); program.slot_count]],
            output: String::new(),
            window,
            scene: Scene::default(),
            camera_3d: false,
            mouse_captured: false,
            io,
            running: "",
            steps: 0,
            at: 0,
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
        self.section("bosonoga_global", &program.sections.global)?;
        self.section("gfx_init", &program.sections.init)?;
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
        self.running = if program.is_gfx {
            "gfx_main"
        } else {
            "bosonoga_main"
        };
        self.steps = 0;
        let flow = self.block(statements);
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
                if let Some(frame) = self.frame(*slot)
                    && let Some(value) = frame.get(slot.index())
                {
                    return value;
                }
                &Value::Bool(false)
            }
        }
    }

    // The frame of a slot: of the running function or section, or the first.
    fn frame(&self, slot: Slot) -> Option<&Vec<Value>> {
        match slot {
            Slot::Local(_) => self.frames.last(),
            Slot::Global(_) => self.frames.first(),
        }
    }

    fn values(&self, operands: &[Operand]) -> Vec<Value> {
        let mut values = Vec::new();
        for operand in operands {
            values.push(self.get(operand).clone());
        }
        values
    }

    fn set(&mut self, slot: Slot, value: Value) {
        let frame = match slot {
            Slot::Local(_) => self.frames.last_mut(),
            Slot::Global(_) => self.frames.first_mut(),
        };
        if let Some(found) = frame
            && let Some(place) = found.get_mut(slot.index())
        {
            *place = value;
        }
    }

    // Sets gfx_key, gfx_delta_time or gfx_heap_bytes, of a section, which runs
    // in the first frame.
    fn set_section_slot(&mut self, slot: Option<Slot>, value: Value) {
        let Some(at) = slot else {
            return;
        };
        if let Some(frame) = self.frames.first_mut()
            && let Some(place) = frame.get_mut(at.index())
        {
            *place = value;
        }
    }

    fn set_mouse_captured(&mut self, captured: bool) {
        self.mouse_captured = captured;
        self.set_section_slot(self.program.mouse_captured_slot, Value::Bool(captured));
    }

    // gfx_mouse_capture or gfx_mouse_release, which must change whether the
    // mouse is captured.
    fn capture_mouse(&mut self, capture: bool, offset: usize) -> Result<(), Diagnostic> {
        if capture == self.mouse_captured {
            let message = if capture {
                "gfx_mouse_capture needs a released mouse, but the mouse is captured"
            } else {
                "gfx_mouse_release needs a captured mouse, but the mouse is released"
            };
            return Err(Diagnostic::new(offset, message));
        }
        self.set_mouse_captured(capture);
        Ok(())
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

    fn section(
        &mut self,
        word: &'static str,
        statements: &[Statement],
    ) -> Result<Flow, Diagnostic> {
        let calls = self.frames.split_off(1);
        let running = mem::replace(&mut self.running, word);
        let steps = mem::replace(&mut self.steps, 0);
        let flow = self.block(statements);
        self.running = running;
        self.steps = steps;
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
        // Measured before the scene of the last frame is let go, which the
        // heap held until now.
        if program.heap_slot.is_some() {
            let bytes =
                super::heap::bytes(&self.frames, &program.arrays, &self.scene, &self.output);
            self.set_section_slot(program.heap_slot, Value::U64(bytes));
        }
        self.scene = Scene {
            clear: program.background,
            shapes: Vec::new(),
            mouse_captured: false,
        };
        self.camera_3d = false;
        self.section("gfx_frame", &program.sections.frame)?;
        self.flush();
        self.scene.mouse_captured = self.mouse_captured;
        let mut shown = None;
        if let Some(window) = self.window.as_deref_mut() {
            shown = window.show(&self.scene);
        }
        let Some(input) = shown else {
            return Ok(Flow::Closed);
        };
        self.set_mouse_captured(input.mouse_captured);
        let [move_x, move_y] = program.mouse.applied(input.mouse_move);
        self.set_section_slot(program.mouse_move_x_slot, Value::F32(move_x));
        self.set_section_slot(program.mouse_move_y_slot, Value::F32(move_y));
        for event in input.keys {
            let (key, word, statements) = match event {
                KeyEvent::Down(key) => (key, "gfx_key_down", &program.sections.key_down),
                KeyEvent::Up(key) => (key, "gfx_key_up", &program.sections.key_up),
            };
            self.set_section_slot(program.key_slot, Value::InputKey(key));
            self.section(word, statements)?;
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
        let start = array.slot.index() + row * array.columns;
        let mut values = Vec::new();
        if let Some(frame) = self.frame(array.slot)
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
        let outer = mem::replace(&mut self.at, offset);
        let returned = self.block(&function.statements);
        self.at = outer;
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
                let color = self.color(value);
                if let Err(message) = space::opaque("gfx_clear", "color", color) {
                    return Err(Diagnostic::new(offset, message));
                }
                self.scene.clear = color;
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
            Draw::Cube(values) => Shape::Cube(self.cube("gfx_draw_cube", values, offset)?),
            Draw::CubeWires(values) => {
                Shape::CubeWires(self.cube("gfx_draw_cube_wires", values, offset)?)
            }
            Draw::Grid([count, apart, axes, lines]) => {
                let mut slices = 0;
                if let Value::U32(value) = self.get(count) {
                    slices = *value;
                }
                let spacing = self.real(apart);
                let (axes_color, color) = (self.color(axes), self.color(lines));
                let checked = space::grid(slices, spacing)
                    .and_then(|()| space::opaque("gfx_draw_grid", "axes color", axes_color))
                    .and_then(|()| space::opaque("gfx_draw_grid", "line color", color));
                if let Err(message) = checked {
                    return Err(Diagnostic::new(offset, message));
                }
                Shape::Grid {
                    slices,
                    spacing,
                    axes_color,
                    color,
                }
            }
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

    fn cube(
        &self,
        word: &str,
        values: &[Operand; 7],
        offset: usize,
    ) -> Result<crate::gfx::Cube, Diagnostic> {
        let [x, y, z, width, height, depth, fill] = values;
        let reals = [x, y, z, width, height, depth].map(|operand| self.real(operand));
        match space::cube(word, reals, self.color(fill)) {
            Ok(cube) => Ok(cube),
            Err(message) => Err(Diagnostic::new(offset, message)),
        }
    }

    // Draws the block with the camera, then goes back to 2D, also when the
    // block fails.
    fn camera(
        &mut self,
        values: &[Operand; 10],
        block: &[Statement],
        offset: usize,
    ) -> Result<Flow, Diagnostic> {
        if self.camera_3d {
            return Err(Diagnostic::new(
                offset,
                "gfx_camera_3d runs at most once per frame",
            ));
        }
        let camera = match space::camera(values.each_ref().map(|operand| self.real(operand))) {
            Ok(camera) => camera,
            Err(message) => return Err(Diagnostic::new(offset, message)),
        };
        self.camera_3d = true;
        self.scene.shapes.push(Shape::Begin3d(camera));
        let flow = self.block(block);
        self.scene.shapes.push(Shape::End3d);
        flow
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
        let start = target.slot.after(row * target.columns);
        let values = self.values(operands);
        for (column, value) in values.into_iter().enumerate() {
            self.set(start.after(column), value);
        }
        Ok(())
    }

    // Counts a statement of a gfx program, whose sections each run at most
    // STEPS_PER_FRAME before the next frame. A text program has no frames.
    fn step(&mut self) -> Result<Flow, Diagnostic> {
        if self.window.is_none() {
            return Ok(Flow::Next);
        }
        self.steps += 1;
        if self.steps <= STEPS_PER_FRAME {
            return Ok(Flow::Next);
        }
        let message = if self.running == "gfx_main" {
            format!("gfx_main ran {STEPS_PER_FRAME} statements without gfx_await_next_frame")
        } else {
            format!(
                "{} ran {STEPS_PER_FRAME} statements, the most that a section of a gfx program runs before the next frame",
                self.running
            )
        };
        Err(Diagnostic::new(self.at, message))
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
        self.take(&mut results, &given.slots);
        let flow = self.block(&given.block);
        self.release(&given.slots);
        flow
    }

    fn invoke(&mut self, invoke: &Invoke) -> Result<Flow, Diagnostic> {
        let mut results = match self.call(&invoke.call)? {
            Flow::Values(values) => values,
            Flow::Next => Vec::new(),
            flow @ (Flow::Exit(_) | Flow::Closed) => return Ok(flow),
        };
        self.take(&mut results, &invoke.slots);
        let flow = self.block(&invoke.block);
        self.release(&invoke.slots);
        flow
    }

    // Moves the taken values into their slots, for a block to read.
    fn take(&mut self, results: &mut [Value], slots: &[(usize, Slot)]) {
        for (index, slot) in slots {
            if let Some(value) = results.get_mut(*index) {
                let taken = mem::replace(value, Value::Bool(false));
                self.set(*slot, taken);
            }
        }
    }

    // Frees the taken values after their block, as a str can be long.
    fn release(&mut self, slots: &[(usize, Slot)]) {
        for (_, slot) in slots {
            self.set(*slot, Value::Bool(false));
        }
    }

    fn repeat(
        &mut self,
        condition: &Condition,
        block: &[Statement],
        offset: usize,
    ) -> Result<Flow, Diagnostic> {
        let outer = mem::replace(&mut self.at, offset);
        let mut flow = Ok(Flow::Next);
        while self.holds(condition) {
            flow = self.block(block);
            if !matches!(flow, Ok(Flow::Next)) {
                break;
            }
        }
        self.at = outer;
        flow
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

    fn join(&mut self, slot: Slot, operands: &[Operand]) {
        let mut joined = String::new();
        for operand in operands {
            joined.push_str(&self.get(operand).to_string());
        }
        self.set(slot, Value::Str(joined));
    }

    fn assign(&mut self, slot: Slot, initial: &Operand, shift_or: &[Operand]) {
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

    fn init_array(&mut self, slot: Slot, values: &[Value], repeat: usize) {
        for row in 0..repeat {
            let start = slot.after(row * values.len());
            for (column, value) in values.iter().enumerate() {
                self.set(start.after(column), value.clone());
            }
        }
    }

    fn statement(&mut self, statement: &Statement) -> Result<Flow, Diagnostic> {
        match statement {
            Statement::Info(operands) => self.info(operands),
            Statement::Join(slot, operands) => self.join(*slot, operands),
            Statement::Given(given) => return self.given(given),
            Statement::Invoke(invoke) => return self.invoke(invoke),
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
            Statement::While {
                condition,
                block,
                offset,
            } => return self.repeat(condition, block, *offset),
            Statement::AwaitNextFrame => return self.next_frame(),
            Statement::Draw { draw, offset } => self.draw(draw, *offset)?,
            Statement::Camera3d {
                camera,
                block,
                offset,
            } => return self.camera(camera, block, *offset),
            Statement::MouseCapture { capture, offset } => self.capture_mouse(*capture, *offset)?,
            Statement::Return(status) => return Ok(Flow::Exit(*status)),
            Statement::ReturnValues(operands) => return Ok(Flow::Values(self.values(operands))),
            Statement::Unwrap { slot, optional } => {
                if let Value::Optional {
                    value: Some(present),
                    ..
                } = self.get(optional)
                {
                    let value = Value::clone(present);
                    self.set(*slot, value);
                }
            }
        }
        Ok(Flow::Next)
    }
}
