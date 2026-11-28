use super::{Checker, Space};
use crate::Diagnostic;
use crate::ast::{self, Section, Span};
use crate::gfx::{Align, Horizontal, Vertical};
use crate::program::{Draw, Operand, Statement};
use crate::value::{Kind, Value};

const CAMERA: &str = "gfx_camera_3d";

fn draw_kinds(draw: &ast::Draw) -> &'static [Kind] {
    match draw {
        ast::Draw::Clear => &[Kind::Color],
        ast::Draw::Rectangle => &[Kind::F32, Kind::F32, Kind::F32, Kind::F32, Kind::Color],
        ast::Draw::Hex | ast::Draw::Circle => &[Kind::F32, Kind::F32, Kind::F32, Kind::Color],
        ast::Draw::Icon => &[Kind::F32, Kind::F32, Kind::Icon, Kind::F32, Kind::Color],
        ast::Draw::Text => &[Kind::F32, Kind::F32, Kind::Font, Kind::F32, Kind::Color],
        ast::Draw::Cube | ast::Draw::CubeWires => &[
            Kind::F32,
            Kind::F32,
            Kind::F32,
            Kind::F32,
            Kind::F32,
            Kind::F32,
            Kind::Color,
        ],
        ast::Draw::Grid => &[Kind::U32, Kind::F32, Kind::Color, Kind::Color],
    }
}

// The next N operands, which the grammar gave in the number that N says.
fn take<const N: usize>(operands: &mut Vec<Operand>) -> [Operand; N] {
    std::array::from_fn(|_| {
        if operands.is_empty() {
            return Operand::Literal(Value::F32(0.0));
        }
        operands.remove(0)
    })
}

// What only gfx_frame has: a draw, a gfx_camera_3d, or a call of a def that
// has one.
pub(super) enum Need<'a> {
    Statement(&'a str),
    Call(&'a str),
}

impl Need<'_> {
    fn outside(&self) -> String {
        match self {
            Need::Statement(word) => {
                format!("{word} can only be in gfx_frame, or in a def that gfx_frame calls")
            }
            Need::Call(name) => format!(
                "'{name}' draws, so it can only be called in gfx_frame, or in a def that gfx_frame calls"
            ),
        }
    }

    fn needs_camera(&self) -> String {
        match self {
            Need::Statement(word) => format!("{word} can only be in the block of {CAMERA}"),
            Need::Call(name) => {
                format!("'{name}' draws in 3D, so it is called in the block of {CAMERA}")
            }
        }
    }

    fn needs_flat(&self) -> String {
        match self {
            Need::Statement(word) => {
                format!("{word} draws in 2D, so it goes before or after the block of {CAMERA}")
            }
            Need::Call(name) => format!(
                "'{name}' draws in 2D, so it is called before or after the block of {CAMERA}"
            ),
        }
    }
}

impl<'input> Checker<'_, 'input> {
    // Checks where a draw is, or a call of a def that draws: in gfx_frame, in
    // 2D or in the block of the camera, or in a def, which then draws there.
    pub(super) fn need_frame(
        &mut self,
        at: usize,
        space: Space,
        need: &Need<'_>,
    ) -> Result<(), Diagnostic> {
        if self.function.is_none() && self.section != Some(Section::GfxFrame) {
            return Self::fail(at, need.outside());
        }
        if self.camera {
            if space == Space::Flat {
                return Self::fail(at, need.needs_flat());
            }
            return Ok(());
        }
        let Some(index) = self.function else {
            if space == Space::Camera {
                return Self::fail(at, need.needs_camera());
            }
            return Ok(());
        };
        let Some(function) = self.functions.get_mut(index) else {
            return Ok(());
        };
        match function.draws {
            None => function.draws = Some(space),
            Some(drawn) if drawn == space => {}
            Some(_) => {
                let name = function.name;
                return Self::fail(
                    at,
                    format!(
                        "'{name}' draws both outside and inside the block of {CAMERA}, so it is two defs, one called before or after that block and one called in it"
                    ),
                );
            }
        }
        Ok(())
    }

    pub(super) fn draw(
        &mut self,
        span: Span,
        draw: &ast::Draw,
        align: Option<Align>,
        values: &[ast::Operand<'input>],
    ) -> Result<Statement, Diagnostic> {
        let word = self.text(span);
        let space = if draw.is_3d() {
            Space::Camera
        } else {
            Space::Flat
        };
        self.need_frame(span.start, space, &Need::Statement(word))?;
        let kinds = draw_kinds(draw);
        let mut operands = Vec::new();
        let mut parts = Vec::new();
        for (index, value) in values.iter().enumerate() {
            if let Some(kind) = kinds.get(index) {
                operands.push(self.value_of_kind(value, *kind, word)?);
            }
        }
        for value in values.iter().skip(kinds.len()) {
            parts.push(self.shown_value(value, word)?);
        }
        let o = &mut operands;
        let checked = match draw {
            ast::Draw::Clear => {
                let [color] = take(o);
                Draw::Clear(color)
            }
            ast::Draw::Rectangle => Draw::Rectangle(take(o)),
            ast::Draw::Hex => Draw::Hex(take(o)),
            ast::Draw::Circle => Draw::Circle(take(o)),
            ast::Draw::Icon => Draw::Icon(take(o)),
            ast::Draw::Text => Draw::Text {
                align: align.unwrap_or(Align {
                    horizontal: Horizontal::Left,
                    vertical: Vertical::Baseline,
                }),
                values: take(o),
                parts,
            },
            ast::Draw::Cube => Draw::Cube(take(o)),
            ast::Draw::CubeWires => Draw::CubeWires(take(o)),
            ast::Draw::Grid => Draw::Grid(take(o)),
        };
        Ok(Statement::Draw {
            draw: checked,
            offset: span.start,
        })
    }

    // gfx_camera_3d position target up fov di ... do: the block draws with
    // this camera, and the frame draws in 2D again after it.
    pub(super) fn camera_3d(
        &mut self,
        span: Span,
        values: &[ast::Operand<'input>],
        block: &[ast::Located<'input>],
    ) -> Result<Statement, Diagnostic> {
        if self.camera {
            return Self::fail(
                span.start,
                format!("{CAMERA} cannot be in the block of another {CAMERA}"),
            );
        }
        self.need_frame(span.start, Space::Flat, &Need::Statement(CAMERA))?;
        let mut operands = Vec::new();
        for value in values {
            operands.push(self.value_of_kind(value, Kind::F32, CAMERA)?);
        }
        self.camera = true;
        let checked = self.block(block);
        self.camera = false;
        Ok(Statement::Camera3d {
            camera: take(&mut operands),
            block: checked?,
            offset: span.start,
        })
    }
}
