use super::Checker;
use crate::Diagnostic;
use crate::ast::{self, Named, Section, Span};
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

impl<'input> Checker<'_, 'input> {
    fn in_frame(&self) -> bool {
        self.section == Some(Section::GfxFrame) && self.function.is_none()
    }

    pub(super) fn draw(
        &mut self,
        span: Span,
        draw: &ast::Draw,
        aligns: Option<&(Named<'input>, Named<'input>)>,
        values: &[ast::Operand<'input>],
    ) -> Result<Statement, Diagnostic> {
        let word = self.text(span);
        if draw.is_3d() && !self.camera {
            return Self::fail(
                span.start,
                format!("{word} can only be in the block of {CAMERA}, in gfx_frame"),
            );
        }
        if !self.in_frame() {
            return Self::fail(span.start, format!("{word} can only be in gfx_frame"));
        }
        if !draw.is_3d() && self.camera {
            return Self::fail(
                span.start,
                format!("{word} draws in 2D, so it goes before or after the block of {CAMERA}"),
            );
        }
        let kinds = draw_kinds(draw);
        let align = self.align(aligns)?;
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
                align,
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
        if !self.in_frame() {
            return Self::fail(span.start, format!("{CAMERA} can only be in gfx_frame"));
        }
        if self.camera {
            return Self::fail(
                span.start,
                format!("{CAMERA} cannot be in the block of another {CAMERA}"),
            );
        }
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

    fn align(&self, aligns: Option<&(Named<'input>, Named<'input>)>) -> Result<Align, Diagnostic> {
        let Some(((h_span, h), (v_span, v))) = aligns else {
            return Ok(Align {
                horizontal: Horizontal::Left,
                vertical: Vertical::Baseline,
            });
        };
        let horizontal = match *h {
            "left" => Horizontal::Left,
            "center" => Horizontal::Center,
            "right" => Horizontal::Right,
            _ => return self.expected_at(*h_span, "left, center or right"),
        };
        let vertical = match *v {
            "top" => Vertical::Top,
            "middle" => Vertical::Middle,
            "baseline" => Vertical::Baseline,
            "bottom" => Vertical::Bottom,
            _ => return self.expected_at(*v_span, "top, middle, baseline or bottom"),
        };
        Ok(Align {
            horizontal,
            vertical,
        })
    }
}
