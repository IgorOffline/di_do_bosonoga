use crate::Program;
use crate::ast::ExitStatus;

// A color is 0xTTRRGGBB, where TT is its transparency: 255 less its alpha.
// So every RRGGBB color is opaque, 0 too, which is black.
#[must_use]
pub fn alpha(color: u32) -> u8 {
    u8::MAX - color.to_be_bytes()[0]
}

#[must_use]
pub fn is_opaque(color: u32) -> bool {
    alpha(color) == u8::MAX
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Horizontal {
    Left,
    Center,
    Right,
}

impl Horizontal {
    #[must_use]
    pub fn named(word: &str) -> Option<Horizontal> {
        Some(match word {
            "gfx_align_x_left" => Horizontal::Left,
            "gfx_align_x_center" => Horizontal::Center,
            "gfx_align_x_right" => Horizontal::Right,
            _ => return None,
        })
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Vertical {
    Top,
    Middle,
    Baseline,
    Bottom,
}

impl Vertical {
    #[must_use]
    pub fn named(word: &str) -> Option<Vertical> {
        Some(match word {
            "gfx_align_y_top" => Vertical::Top,
            "gfx_align_y_middle" => Vertical::Middle,
            "gfx_align_y_baseline" => Vertical::Baseline,
            "gfx_align_y_bottom" => Vertical::Bottom,
            _ => return None,
        })
    }
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
    // Whether the program wants the mouse captured: the cursor grabbed and
    // hidden. The window captures or releases it when it shows the scene.
    pub mouse_captured: bool,
}

// A key that went down or came up, by its code. A key comes up only after it
// went down, and every key that is down comes up when the window loses focus.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum KeyEvent {
    Down(u32),
    Up(u32),
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Input {
    // In the order they happened since the last frame.
    pub keys: Vec<KeyEvent>,
    pub delta_time: f32,
    // The pixels the mouse moved, x and y, since the last frame. They are 0
    // unless the mouse is captured, and in the frames where it was captured
    // or released, so that the view never jumps.
    pub mouse_move: [f32; 2],
    // Whether the mouse is captured now. The window can release it by itself,
    // as when it loses focus.
    pub mouse_captured: bool,
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
