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

// A perspective camera, in world units. Y is up for gfx_draw_grid, but the
// camera can have any up that is not along its view.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Camera {
    pub position: [f32; 3],
    pub target: [f32; 3],
    pub up: [f32; 3],
    // The vertical field of view, in degrees, above 0 and below 180.
    pub fov_degrees: f32,
}

// A box from center - size / 2 to center + size / 2.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Cube {
    pub center: [f32; 3],
    pub size: [f32; 3],
    pub color: u32,
}

// The shapes of a frame in the order they are drawn. Begin3d and End3d come
// in pairs, at most once per frame, and only cubes and grids are between them.
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
    Begin3d(Camera),
    // Shaded faces: the top in full color, the sides darker, the bottom darkest.
    Cube(Cube),
    CubeWires(Cube),
    // Lines on the plane y = 0 around the origin, slices / 2 cells to each
    // side, spacing apart. The two lines through the origin have axes_color.
    Grid {
        slices: u32,
        spacing: f32,
        axes_color: u32,
        color: u32,
    },
    End3d,
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
