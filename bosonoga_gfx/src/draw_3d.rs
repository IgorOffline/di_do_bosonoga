// The 3D of a scene: its camera block, cubes and grids.

use bosonoga::gfx::{Camera, Cube};
use macroquad::camera::{Camera3D, set_camera, set_default_camera};
use macroquad::color::Color;
use macroquad::math::{Vec3, vec3};
use macroquad::models::{Mesh, Vertex, draw_cube_wires, draw_grid, draw_mesh};

use crate::draw::color;

// The faces of a cube as the direction they face, the brightness of their
// color, and their corners as signs of the half size, counterclockwise
// from outside. There is no lighting, so the shades are fixed as by a sun
// above and a little to +x and +z, and two faces that meet never match.
const FACES: [([f32; 3], [[f32; 3]; 4]); 6] = [
    (
        [1.0, 0.0, 0.0],
        [
            [1.0, -1.0, 1.0],
            [1.0, -1.0, -1.0],
            [1.0, 1.0, -1.0],
            [1.0, 1.0, 1.0],
        ],
    ),
    (
        [0.0, 1.0, 0.0],
        [
            [-1.0, 1.0, 1.0],
            [1.0, 1.0, 1.0],
            [1.0, 1.0, -1.0],
            [-1.0, 1.0, -1.0],
        ],
    ),
    (
        [0.0, 0.0, 1.0],
        [
            [-1.0, -1.0, 1.0],
            [1.0, -1.0, 1.0],
            [1.0, 1.0, 1.0],
            [-1.0, 1.0, 1.0],
        ],
    ),
    (
        [-1.0, 0.0, 0.0],
        [
            [-1.0, -1.0, -1.0],
            [-1.0, -1.0, 1.0],
            [-1.0, 1.0, 1.0],
            [-1.0, 1.0, -1.0],
        ],
    ),
    (
        [0.0, -1.0, 0.0],
        [
            [-1.0, -1.0, -1.0],
            [1.0, -1.0, -1.0],
            [1.0, -1.0, 1.0],
            [-1.0, -1.0, 1.0],
        ],
    ),
    (
        [0.0, 0.0, -1.0],
        [
            [1.0, -1.0, -1.0],
            [-1.0, -1.0, -1.0],
            [-1.0, 1.0, -1.0],
            [1.0, 1.0, -1.0],
        ],
    ),
];
const SHADES: [f32; 6] = [0.82, 1.0, 0.72, 0.62, 0.45, 0.55];
const FACE_INDICES: [u16; 6] = [0, 1, 2, 0, 2, 3];
const CORNERS_PER_FACE: u16 = 4;

fn point(values: [f32; 3]) -> Vec3 {
    vec3(values[0], values[1], values[2])
}

fn shaded(rgb: u32, shade: f32) -> Color {
    let full = color(rgb);
    Color::new(full.r * shade, full.g * shade, full.b * shade, full.a)
}

pub(crate) fn begin(camera: &Camera) {
    set_camera(&Camera3D {
        position: point(camera.position),
        target: point(camera.target),
        up: point(camera.up),
        fovy: camera.fov_degrees.to_radians(),
        ..Camera3D::default()
    });
}

pub(crate) fn end() {
    set_default_camera();
}

pub(crate) fn cube(cube: &Cube) {
    let center = point(cube.center);
    let half = point(cube.size) / 2.0;
    let mut vertices = Vec::with_capacity(FACES.len() * usize::from(CORNERS_PER_FACE));
    let mut indices = Vec::with_capacity(FACES.len() * FACE_INDICES.len());
    let mut first = 0;
    for ((_, corners), shade) in FACES.iter().zip(SHADES) {
        let face_color = shaded(cube.color, shade);
        for corner in corners {
            let at = center + point(*corner) * half;
            vertices.push(Vertex::new(at.x, at.y, at.z, 0.0, 0.0, face_color));
        }
        for index in FACE_INDICES {
            indices.push(first + index);
        }
        first += CORNERS_PER_FACE;
    }
    draw_mesh(&Mesh {
        vertices,
        indices,
        texture: None,
    });
}

pub(crate) fn cube_wires(cube: &Cube) {
    draw_cube_wires(point(cube.center), point(cube.size), color(cube.color));
}

pub(crate) fn grid(slices: u32, spacing: f32, axes_color: u32, rgb: u32) {
    draw_grid(slices, spacing, color(axes_color), color(rgb));
}

#[cfg(test)]
mod tests {
    use super::{FACES, SHADES};

    fn dot(a: [f32; 3], b: [f32; 3]) -> f32 {
        a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
    }

    // Each face has its four corners on its own side, counterclockwise from
    // outside, and faces that share an edge have different shades.
    #[test]
    fn faces() {
        for (normal, corners) in FACES {
            for corner in corners {
                assert!((dot(normal, corner) - 1.0).abs() < f32::EPSILON);
            }
            for turn in 0..4 {
                let [first, second, third] = [0, 1, 2].map(|step| corners[(turn + step) % 4]);
                let along = [0, 1, 2].map(|axis| second[axis] - first[axis]);
                let next = [0, 1, 2].map(|axis| third[axis] - second[axis]);
                let cross = [
                    along[1] * next[2] - along[2] * next[1],
                    along[2] * next[0] - along[0] * next[2],
                    along[0] * next[1] - along[1] * next[0],
                ];
                assert!(dot(cross, normal) > 0.0, "{normal:?}");
            }
        }
        for (a, (normal_a, _)) in FACES.iter().enumerate() {
            for (b, (normal_b, _)) in FACES.iter().enumerate() {
                if dot(*normal_a, *normal_b).abs() < f32::EPSILON {
                    assert!((SHADES[a] - SHADES[b]).abs() > 0.05, "{a} {b}");
                }
            }
        }
    }
}
