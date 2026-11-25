// The checks of what a frame draws in 3D, so that the window only gets a
// camera, cubes and grids that macroquad draws as they were meant. Each
// error names the statement and the value that is wrong.

use crate::gfx::{Camera, Cube, is_opaque};
use crate::value::Value;

const CAMERA: &str = "gfx_camera_3d";
const GRID: &str = "gfx_draw_grid";
const GRID_SLICES_MAX: u32 = 256;
const FOV_DEGREES_MAX: f32 = 180.0;
// How far the up of a camera must be from its view: the sine of the angle
// between them, about 0.06 degrees.
const UP_SINE_MIN: f64 = 1e-3;

const CAMERA_NAMES: [&str; 10] = [
    "position x",
    "position y",
    "position z",
    "target x",
    "target y",
    "target z",
    "up x",
    "up y",
    "up z",
    "field of view",
];
const CUBE_NAMES: [&str; 6] = ["x", "y", "z", "width", "height", "depth"];

fn finite(word: &str, names: &[&str], values: &[f32]) -> Result<(), String> {
    for (name, value) in names.iter().zip(values) {
        if !value.is_finite() {
            return Err(format!(
                "{word} needs finite values, but its {name} is {value}"
            ));
        }
    }
    Ok(())
}

fn length_squared(v: [f32; 3]) -> f32 {
    v[0] * v[0] + v[1] * v[1] + v[2] * v[2]
}

// The vector at full precision, of length 1.
fn unit(v: [f32; 3]) -> [f64; 3] {
    let length = f64::from(length_squared(v)).sqrt();
    v.map(|c| f64::from(c) / length)
}

// The length of v, which must be finite and above 0 in f32, as the camera of
// the window computes in f32.
fn usable(v: [f32; 3], zero: &str, overflow: &str) -> Result<(), String> {
    let squared = length_squared(v);
    if !squared.is_finite() {
        return Err(format!("{CAMERA} {overflow}"));
    }
    if squared < f32::MIN_POSITIVE {
        return Err(format!("{CAMERA} {zero}"));
    }
    Ok(())
}

pub(super) fn camera(values: [f32; 10]) -> Result<Camera, String> {
    finite(CAMERA, &CAMERA_NAMES, &values)?;
    let [px, py, pz, tx, ty, tz, ux, uy, uz, fov_degrees] = values;
    let camera = Camera {
        position: [px, py, pz],
        target: [tx, ty, tz],
        up: [ux, uy, uz],
        fov_degrees,
    };
    if fov_degrees <= 0.0 || fov_degrees >= FOV_DEGREES_MAX {
        return Err(format!(
            "{CAMERA} needs a field of view above 0 and below 180 degrees, not {fov_degrees}"
        ));
    }
    let view = [tx - px, ty - py, tz - pz];
    usable(
        view,
        "needs its target apart from its position",
        "has its target too far from its position",
    )?;
    usable(
        camera.up,
        "needs an up that is not 0 0 0",
        "has an up that is too long",
    )?;
    let (d, u) = (unit(view), unit(camera.up));
    let cross = [
        d[1] * u[2] - d[2] * u[1],
        d[2] * u[0] - d[0] * u[2],
        d[0] * u[1] - d[1] * u[0],
    ];
    let sine = (cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]).sqrt();
    if sine < UP_SINE_MIN {
        return Err(format!(
            "{CAMERA} needs an up that is not along its view from position to target"
        ));
    }
    Ok(camera)
}

// 3D is drawn without sorting by depth, so its colors must be opaque, and so
// must the color that gfx_clear clears a frame with.
pub(super) fn opaque(word: &str, name: &str, color: u32) -> Result<(), String> {
    if is_opaque(color) {
        return Ok(());
    }
    Err(format!(
        "{word} needs an opaque color, as RRGGBB, but its {name} is {}",
        Value::Color(color)
    ))
}

pub(super) fn cube(word: &str, values: [f32; 6], color: u32) -> Result<Cube, String> {
    finite(word, &CUBE_NAMES, &values)?;
    opaque(word, "color", color)?;
    let [x, y, z, width, height, depth] = values;
    for (name, size) in CUBE_NAMES.iter().zip(&values).skip(3) {
        if *size <= 0.0 {
            return Err(format!(
                "{word} needs a width, height and depth above 0, but its {name} is {size}"
            ));
        }
    }
    Ok(Cube {
        center: [x, y, z],
        size: [width, height, depth],
        color,
    })
}

// Checks a grid of slices and spacing: an even count, so that the lines
// through the origin are the middle ones, and a size that f32 holds.
pub(super) fn grid(slices: u32, spacing: f32) -> Result<(), String> {
    if !(2..=GRID_SLICES_MAX).contains(&slices) || slices % 2 != 0 {
        return Err(format!(
            "{GRID} needs an even slice count from 2 to {GRID_SLICES_MAX}, not {slices}"
        ));
    }
    if !spacing.is_finite() || spacing <= 0.0 {
        return Err(format!(
            "{GRID} needs a finite spacing above 0, not {spacing}"
        ));
    }
    let half = u16::try_from(slices / 2).map_or(f32::INFINITY, f32::from);
    if !(half * spacing).is_finite() {
        return Err(format!("{GRID} is wider than f32 holds"));
    }
    Ok(())
}
