use crate::Program;
use crate::ast::ExitStatus;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Horizontal {
    Left,
    Center,
    Right,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Vertical {
    Top,
    Middle,
    Baseline,
    Bottom,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Align {
    pub horizontal: Horizontal,
    pub vertical: Vertical,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Shape {
    Rectangle {
        x: f32,
        y: f32,
        width: f32,
        height: f32,
        color: u32,
    },
    Hex {
        x: f32,
        y: f32,
        radius: f32,
        color: u32,
    },
    Circle {
        x: f32,
        y: f32,
        radius: f32,
        color: u32,
    },
    Icon {
        x: f32,
        y: f32,
        icon: usize,
        angle: f32,
        color: u32,
    },
    Text {
        x: f32,
        y: f32,
        font: usize,
        size: f32,
        color: u32,
        align: Align,
        text: String,
    },
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Scene {
    pub clear: u32,
    pub shapes: Vec<Shape>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Input {
    pub keys: Vec<u32>,
    pub delta_time: f32,
}

pub trait Window {
    fn info(&mut self, text: &str);

    fn show(&mut self, scene: &Scene) -> Option<Input>;
}

pub fn run(program: &Program, window: &mut dyn Window) -> Result<ExitStatus, String> {
    run_with_io(program, window, &mut crate::io::FileIo)
}

pub fn run_with_io(
    program: &Program,
    window: &mut dyn Window,
    io: &mut dyn crate::io::Io,
) -> Result<ExitStatus, String> {
    if !program.is_gfx() {
        return Err(format!("{}: a gfx app needs gfx_main", program.path()));
    }
    program.run_gfx(window, io)
}
