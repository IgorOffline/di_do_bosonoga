use bosonoga::gfx::{Align, Horizontal, Scene, Shape, Vertical};
use macroquad::color::Color;
use macroquad::shapes::{draw_poly, draw_rectangle};
use macroquad::text::{TextParams, draw_text_ex, measure_text};
use macroquad::texture::{DrawTextureParams, Texture2D, draw_texture_ex};
use macroquad::window::clear_background;

use crate::assets::{Assets, Typeface};

const HEX_CORNERS: u8 = 6;
// Enough that a circle of a few dozen pixels looks round.
const CIRCLE_CORNERS: u8 = 48;

fn color(rgb: u32) -> Color {
    let [_, red, green, blue] = rgb.to_be_bytes();
    Color::from_rgba(red, green, blue, u8::MAX)
}

#[expect(
    clippy::as_conversions,
    clippy::cast_possible_truncation,
    clippy::cast_sign_loss,
    reason = "the size is rounded, positive and far below u16::MAX"
)]
fn font_size(pixels: f32) -> u16 {
    pixels.round().clamp(1.0, f32::from(u16::MAX)) as u16
}

pub(crate) fn scene(scene: &Scene, assets: &Assets) {
    clear_background(color(scene.clear));
    for shape in &scene.shapes {
        match shape {
            Shape::Rectangle {
                x,
                y,
                width,
                height,
                color: rgb,
            } => draw_rectangle(*x, *y, *width, *height, color(*rgb)),
            Shape::Hex {
                x,
                y,
                radius,
                color: rgb,
            } => draw_poly(*x, *y, HEX_CORNERS, *radius, 0.0, color(*rgb)),
            Shape::Circle {
                x,
                y,
                radius,
                color: rgb,
            } => draw_poly(*x, *y, CIRCLE_CORNERS, *radius, 0.0, color(*rgb)),
            Shape::Icon {
                x,
                y,
                icon,
                angle,
                color: rgb,
            } => {
                if let Some(texture) = assets.icons.get(*icon) {
                    draw_icon(texture, *x, *y, *angle, *rgb);
                }
            }
            Shape::Text {
                x,
                y,
                font,
                size,
                color: rgb,
                align,
                text,
            } => {
                if let Some(typeface) = assets.typefaces.get(*font) {
                    draw_aligned_text(typeface, text, *x, *y, *size, *rgb, *align);
                }
            }
        }
    }
}

fn draw_icon(texture: &Texture2D, x: f32, y: f32, angle: f32, rgb: u32) {
    let half_width = texture.width() / 2.0;
    let half_height = texture.height() / 2.0;
    draw_texture_ex(
        texture,
        (x - half_width).round(),
        (y - half_height).round(),
        color(rgb),
        DrawTextureParams {
            rotation: angle,
            ..DrawTextureParams::default()
        },
    );
}

fn draw_aligned_text(
    typeface: &Typeface,
    text: &str,
    x: f32,
    y: f32,
    size: f32,
    rgb: u32,
    align: Align,
) {
    let em = size * typeface.em_per_height;
    let rasterized = font_size(em);
    let scale = em / f32::from(rasterized);
    let width = measure_text(text, Some(&typeface.font), rasterized, scale).width;
    let left = match align.horizontal {
        Horizontal::Left => x,
        Horizontal::Center => x - width / 2.0,
        Horizontal::Right => x - width,
    };
    let baseline = match align.vertical {
        Vertical::Baseline => y,
        Vertical::Top => y + size * typeface.ascender,
        Vertical::Middle => y + size * (typeface.ascender + typeface.descender) / 2.0,
        Vertical::Bottom => y + size * typeface.descender,
    };
    draw_text_ex(
        text,
        left,
        baseline,
        TextParams {
            font: Some(&typeface.font),
            font_size: rasterized,
            font_scale: scale,
            color: color(rgb),
            ..TextParams::default()
        },
    );
}
