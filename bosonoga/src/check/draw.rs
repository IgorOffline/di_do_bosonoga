use super::Checker;
use crate::Diagnostic;
use crate::ast::{self, Named, Section, Span};
use crate::gfx::{Align, Horizontal, Vertical};
use crate::program::{Draw, Operand, Statement};
use crate::value::{Kind, Value};

fn draw_kinds(draw: &ast::Draw) -> &'static [Kind] {
    match draw {
        ast::Draw::Clear => &[Kind::Color],
        ast::Draw::Rectangle => &[Kind::F32, Kind::F32, Kind::F32, Kind::F32, Kind::Color],
        ast::Draw::Hex | ast::Draw::Circle => &[Kind::F32, Kind::F32, Kind::F32, Kind::Color],
        ast::Draw::Icon => &[Kind::F32, Kind::F32, Kind::Icon, Kind::F32, Kind::Color],
        ast::Draw::Text => &[Kind::F32, Kind::F32, Kind::Font, Kind::F32, Kind::Color],
    }
}

fn take_first(operands: &mut Vec<Operand>) -> Operand {
    if operands.is_empty() {
        return Operand::Literal(Value::F32(0.0));
    }
    operands.remove(0)
}

impl<'input> Checker<'_, 'input> {
    pub(super) fn draw(
        &mut self,
        span: Span,
        draw: &ast::Draw,
        aligns: Option<&(Named<'input>, Named<'input>)>,
        values: &[ast::Operand<'input>],
    ) -> Result<Statement, Diagnostic> {
        let word = self.text(span);
        if self.section != Some(Section::GfxFrame) || self.function.is_some() {
            return Self::fail(span.start, format!("{word} can only be in gfx_frame"));
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
            let (part, _) = self.value(value)?;
            parts.push(part);
        }
        let o = &mut operands;
        let checked = match draw {
            ast::Draw::Clear => Draw::Clear(take_first(o)),
            ast::Draw::Rectangle => Draw::Rectangle([
                take_first(o),
                take_first(o),
                take_first(o),
                take_first(o),
                take_first(o),
            ]),
            ast::Draw::Hex => {
                Draw::Hex([take_first(o), take_first(o), take_first(o), take_first(o)])
            }
            ast::Draw::Circle => {
                Draw::Circle([take_first(o), take_first(o), take_first(o), take_first(o)])
            }
            ast::Draw::Icon => Draw::Icon([
                take_first(o),
                take_first(o),
                take_first(o),
                take_first(o),
                take_first(o),
            ]),
            ast::Draw::Text => Draw::Text {
                align,
                values: [
                    take_first(o),
                    take_first(o),
                    take_first(o),
                    take_first(o),
                    take_first(o),
                ],
                parts,
            },
        };
        Ok(Statement::Draw {
            draw: checked,
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
