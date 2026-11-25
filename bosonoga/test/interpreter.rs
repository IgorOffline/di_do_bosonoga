use std::path::Path;

use bosonoga::Program;
use bosonoga::ast::ExitStatus;
use bosonoga::gfx::{Camera, Cube, Input, KeyEvent, Scene, Shape, Window};
use bosonoga::lega::Lega;
use bosonoga::setting::Settings;

#[cfg(test)]
fn load(path: &str) -> Program {
    let settings = Settings::read(Path::new("test/regina.ini")).unwrap();
    let lega = Lega::read(Path::new("test")).unwrap();
    bosonoga::load(Path::new(path), &lega, &settings).unwrap_or_else(|message| panic!("{message}"))
}

#[test]
fn programs() {
    let programs = [
        ("test/arithmetic.bosonoga", ""),
        ("test/function.bosonoga", ""),
        ("test/var_array.bosonoga", ""),
        (
            "test/call.bosonoga",
            "hello\ncountdown 2\ncountdown 1\ncountdown 0\n",
        ),
        (
            "test/from_global.bosonoga",
            "added 11 as 1\nadded 22 as 2\n",
        ),
        (
            "test/optional.bosonoga",
            "seven 7\nonly seven\nfirst\nbest 9\n",
        ),
    ];
    for (path, expected) in programs {
        let program = load(path);
        match bosonoga::run(&program) {
            Ok((output, status)) => {
                assert_eq!(output, expected, "{path}");
                assert_eq!(status, ExitStatus::Success, "{path}");
            }
            Err((_, diagnostic)) => panic!("{}", program.located(&diagnostic)),
        }
    }
}

#[test]
fn lega() {
    assert_eq!(
        Lega::read(Path::new("src")).unwrap_err(),
        "src/lega.bosonoga: No such file or directory, and every bosonoga application has a lega.bosonoga in its root"
    );
    assert_eq!(
        Lega::parse("lega.bosonoga", "lega di\nlega didi").unwrap_err(),
        "lega.bosonoga:2:6: 'didi' cannot be lega, as bosonoga has no didi"
    );
}

#[test]
fn diagnostics() {
    let lega = Lega::read(Path::new("test")).unwrap();
    let cases = [
        (
            "bosonoga_main di info 1_i32 do",
            "bosonoga_main can reach its end without a return, which gives exit_success or exit_failure",
        ),
        (
            "bosonoga_main di return exit_success info 1_i32 do",
            "this is never reached, as the statement before it always returns",
        ),
        (
            "def f_fn n_i32 ret_void di set n_i32 to 1_i32 do",
            "'n_i32' is a parameter, which is a val and cannot be set, but a copy can, as in var n is n_i32",
        ),
        // The step lega comes first, so the missing return is not reported.
        (
            "bosonoga_main di gfx_draw_hex do",
            "'gfx_draw_hex' is not lega, as lega.bosonoga has no 'lega gfx_draw_hex'",
        ),
        (
            "bosonoga_main di info 7_u64 do",
            "'7_u64' is not lega, as lega.bosonoga has no 'lega _u64'",
        ),
        (
            "bosonoga_main di break do",
            "'break' cannot be lega, as bosonoga has no break",
        ),
    ];
    for (source, expected) in cases {
        match bosonoga::parse(source, &lega) {
            Ok(_) => panic!("expected a diagnostic for: {source}"),
            Err(diagnostic) => assert_eq!(diagnostic.message, expected),
        }
    }
}

// Gives one input per frame, keeps the scenes, and closes after the last input.
#[cfg(test)]
struct Frames {
    inputs: Vec<Input>,
    scenes: Vec<Scene>,
}

impl Window for Frames {
    fn info(&mut self, _text: &str) {}

    fn show(&mut self, scene: &Scene) -> Option<Input> {
        let input = self.inputs.get(self.scenes.len()).cloned();
        self.scenes.push(scene.clone());
        input
    }
}

#[cfg(test)]
fn run_gfx(path: &str, inputs: Vec<Input>) -> (Result<ExitStatus, String>, Vec<Scene>) {
    let mut window = Frames {
        inputs,
        scenes: Vec::new(),
    };
    let result = bosonoga::gfx::run(&load(path), &mut window);
    (result, window.scenes)
}

#[test]
fn gfx_step_limit() {
    let (result, _) = run_gfx("test/gfx_step_limit.bosonoga", vec![Input::default(); 2]);
    assert_eq!(
        result.unwrap_err(),
        "test/gfx_step_limit.bosonoga:6:3: gfx_main ran 100000 statements without gfx_await_next_frame"
    );
}

// The aka_i32 functions that gfx_frame calls place the camera and slice the grid.
#[test]
fn gfx_aka_camera_3d() {
    let (result, scenes) = run_gfx("test/gfx_aka_camera_3d.bosonoga", vec![Input::default(); 2]);
    assert_eq!(result, Ok(ExitStatus::Success));
    let (cube, grid) = (0x00E0_7A5F, 0x005A_5A5A);
    let expected = [
        Shape::Begin3d(Camera {
            position: [6.0, 4.0, -5.0],
            target: [0.0, 0.0, 0.0],
            up: [0.0, 1.0, 0.0],
            fov_degrees: 89.5,
        }),
        Shape::Grid {
            slices: 8,
            spacing: 1.0,
            axes_color: cube,
            color: grid,
        },
        Shape::Cube(Cube {
            center: [0.0, 1.0, 2.0],
            size: [3.0, 4.0, 5.0],
            color: cube,
        }),
        Shape::End3d,
        Shape::Circle {
            x: 5.0,
            y: 6.0,
            radius: 3.0,
            color: grid,
        },
    ];
    assert_eq!(scenes.len(), 2);
    for scene in &scenes {
        assert_eq!(scene.shapes, expected);
    }
}

#[test]
fn gfx_key_and_mouse() {
    let walk = bosonoga::key::named("SAPP_KEYCODE_W").unwrap();
    let mouse = bosonoga::key::named("SAPP_KEYCODE_M").unwrap();
    let input = |keys: &[KeyEvent], mouse_move: [f32; 2], mouse_captured: bool| Input {
        keys: keys.to_vec(),
        delta_time: 0.0,
        mouse_move,
        mouse_captured,
    };
    let inputs = vec![
        input(&[KeyEvent::Down(walk)], [0.0, 0.0], false),
        input(&[KeyEvent::Down(mouse)], [0.0, 0.0], false),
        input(
            &[KeyEvent::Up(walk), KeyEvent::Down(walk), KeyEvent::Up(walk)],
            [3.0, -4.0],
            true,
        ),
        // The window lost focus, which releases the mouse.
        input(&[], [0.0, 0.0], false),
        input(&[KeyEvent::Down(mouse)], [0.0, 0.0], false),
        input(&[], [0.0, 0.0], true),
    ];
    let (result, scenes) = run_gfx("test/gfx_key_and_mouse.bosonoga", inputs);
    assert_eq!(result, Ok(ExitStatus::Success));
    let mut captured = Vec::new();
    for scene in &scenes {
        captured.push(scene.mouse_captured);
    }
    assert_eq!(captured, [false, false, true, true, false, true]);
}
