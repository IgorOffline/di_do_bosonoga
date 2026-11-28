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
        ("test/une.bosonoga", ""),
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
        ("test/then.bosonoga", "rows 2304 4 10 C1 6 3 4\n"),
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
            "main di info 1_i32 do",
            "main can reach its end without a return, which gives exit_success or exit_failure",
        ),
        (
            "main di return exit_success info 1_i32 do",
            "this is never reached, as the statement before it always returns",
        ),
        (
            "def f_fn n_i32 ret_void di set n_i32 to 1_i32 do",
            "'n_i32' is a parameter, which is a val and cannot be set, but a copy can, as in var n is n_i32",
        ),
        (
            "main di var une is 1_i32 return exit_success do",
            "expected a variable name, got 'une'",
        ),
        // The step lega comes first, so the missing return is not reported.
        (
            "main di gfx_draw_hex do",
            "'gfx_draw_hex' is not lega, as lega.bosonoga has no 'lega gfx_draw_hex'",
        ),
        (
            "main di info 7_u64 do",
            "'7_u64' is not lega, as lega.bosonoga has no 'lega _u64'",
        ),
        (
            "main di break do",
            "'break' cannot be lega, as bosonoga has no break",
        ),
        (
            "gfx_frame di gfx_draw_text gfx_align_y_top do",
            "expected gfx_align_x_left, gfx_align_x_center or gfx_align_x_right, got 'gfx_align_y_top'",
        ),
    ];
    assert_diagnostics(&lega, &cases);
}

#[cfg(test)]
fn assert_diagnostics(lega: &Lega, cases: &[(&str, &str)]) {
    for (source, expected) in cases {
        match bosonoga::parse(source, lega) {
            Ok(_) => panic!("expected a diagnostic for: {source}"),
            Err(diagnostic) => assert_eq!(diagnostic.message, *expected),
        }
    }
}

// then, constants, for, pass, gfx_key_held and the defs that draw.
#[test]
fn diagnostics_of_steps_and_frames() {
    let lega = Lega::read(Path::new("test")).unwrap();
    let cases = [
        (
            "main di var x is 1_i32 set x to_add 1_i32 then to_multiply x info x return exit_success do",
            "then to_multiply starts from the value so far, so 'x' is not repeated",
        ),
        (
            "main di var x is 1_i32 set x to 2_i32 then to_f32 info x return exit_success do",
            "'x' is i32, but its steps end with f32",
        ),
        (
            "main di val s is \"a\"_str then to_add 1_i32 info s return exit_success do",
            "then to_add needs a number, but the value so far is str",
        ),
        (
            "main di val s is 1_i32 then to_join 1_i32 info s return exit_success do",
            "then to_join needs a str, but the value so far is i32",
        ),
        (
            "main di val s is 1_i32 then math_to_max info s return exit_success do",
            "expected a value, got 'info'",
        ),
        (
            "const c is 1_i32 then to_divide 0_i32 main di info c return exit_success do",
            "division by zero",
        ),
        (
            "global di var g is 1_i32 do const c is 1_i32 then to_add g main di info c return exit_success do",
            "a constant is known before the program runs, so it is computed from literals, earlier constants and settings, not 'g'",
        ),
        (
            "main di for i from 0d0_f32 to 1d0_f32 di info i do return exit_success do",
            "for counts in i32, i64, u32 or u64, but '0d0_f32' is f32",
        ),
        (
            "main di for i from 0_i32 to 3_i32 di set i to 1_i32 do return exit_success do",
            "'i' is a val and cannot be set",
        ),
        (
            "main di none return exit_success do",
            "'none' cannot be lega, as bosonoga has no none, and pass is the statement that does nothing",
        ),
        (
            "main di do",
            "empty block, write pass for a block that does nothing",
        ),
        (
            "main di info gfx_width return exit_success do",
            "gfx_width is the width in the group gfx of regina.ini, which has none",
        ),
        (
            "main di if gfx_key_held key_walk di pass do return exit_success do",
            "gfx_key_held can only be in gfx_frame, gfx_key_down or gfx_key_up, or in a def that they call",
        ),
        (
            "def scene_fn ret_void di gfx_camera_3d 0d0_f32 0d0_f32 1d0_f32 0d0_f32 0d0_f32 0d0_f32 0d0_f32 1d0_f32 0d0_f32 60d0_f32 di pass do do main di call scene_fn return exit_success do",
            "'scene_fn' draws, so it can only be called in gfx_frame, or in a def that gfx_frame calls",
        ),
        (
            "def scene_fn ret_void di gfx_camera_3d 0d0_f32 0d0_f32 1d0_f32 0d0_f32 0d0_f32 0d0_f32 0d0_f32 1d0_f32 0d0_f32 60d0_f32 di pass do do gfx_frame di gfx_camera_3d 0d0_f32 0d0_f32 1d0_f32 0d0_f32 0d0_f32 0d0_f32 0d0_f32 1d0_f32 0d0_f32 60d0_f32 di call scene_fn do do gfx_main di gfx_await_next_frame return exit_success do",
            "'scene_fn' draws in 2D, so it is called before or after the block of gfx_camera_3d",
        ),
    ];
    assert_diagnostics(&lega, &cases);
}

// The named values of defs and given operations, and the named columns of
// arrays.
#[test]
fn diagnostics_of_named_values() {
    let lega = Lega::read(Path::new("test")).unwrap();
    let grid = "main di var grid is array count_i32 marked_bool with_len_col 2_i32 ";
    let two = "def two_fn ret first_i32 second_i32 di return 1_i32 2_i32 do main di ";
    let cases = [
        (
            "val pairs is array 0_str 1_i32 di row \"a\"_str 1_i32 do main di return exit_success do".to_owned(),
            "'0_str' is not a str, and a value of a def, a column of an array or a value that with ret takes is named with its type, as in value_str",
        ),
        (
            "val pairs is array name_str name_str di row \"a\"_str \"b\"_str do main di return exit_success do".to_owned(),
            "'name_str' is already a column of this array",
        ),
        (
            "val pairs is array name di row \"a\"_str do main di return exit_success do".to_owned(),
            "expected a column whose name ends with its type, like count_i32, got 'name'",
        ),
        (
            format!("{grid}call grid at 0_i32 with ret count_i32 count_i32 as again di info count_i32 again do return exit_success do"),
            "'count_i32' is already taken",
        ),
        (
            "def f_fn count_i32 ret_void di var grid is array count_i32 marked_bool with_len_col 2_i32 call grid at 0_i32 with ret count_i32 di info count_i32 do do main di call f_fn of 1_i32 return exit_success do".to_owned(),
            "variable 'count_i32' already exists, so take it as another name, as in count_i32 as count",
        ),
        (
            format!("{grid}call grid at 0_i32 with ret total_i32 di info total_i32 do return exit_success do"),
            "'grid' has the columns count_i32 marked_bool, so it has no total_i32",
        ),
        (
            format!("{grid}val c is grid at 0_i32 with ret 0_i32 info c return exit_success do"),
            "'grid' has the columns count_i32 marked_bool, which with ret names, as in grid at 0_i32 with ret count_i32, not '0_i32'",
        ),
        (
            format!("{grid}call grid at 0_i32 return exit_success do"),
            "'grid' has the columns count_i32 marked_bool, which call ... with ret takes for its block, as in call grid at 0_i32 with ret count_i32 di ... do, or val takes one of, as in val value is grid at 0_i32 with ret count_i32",
        ),
        (
            format!("{grid}set grid at 0_i32 total_i32 to 1_i32 return exit_success do"),
            "'grid' has the columns count_i32 marked_bool, so it has no total_i32",
        ),
        (
            format!("{grid}set grid at 0_i32 marked_bool to_add 1_i32 return exit_success do"),
            "'marked_bool' is bool, which cannot be used in arithmetic, but set ... to sets it",
        ),
        (
            format!("{grid}set grid to 1_i32 return exit_success do"),
            "'grid' is an array, whose rows are set with set grid at INDEX to and one value per column, and one column with set grid at INDEX count_i32 to",
        ),
        (
            "def two_fn ret 0_i32 1_i32 di return 1_i32 2_i32 do main di return exit_success do".to_owned(),
            "the values that a def returns are named with their type, as in value_i32, not numbered, as '0_i32'",
        ),
        (
            "def two_fn ret first_i32 first_i32 di return 1_i32 2_i32 do main di return exit_success do".to_owned(),
            "'first_i32' is already a value of this def",
        ),
        (
            "def f_fn ret di pass do main di return exit_success do".to_owned(),
            "expected a value named with its type, like count_i32, got 'di'",
        ),
        (
            "def two_fn ret first_i32 second_i32 di return 1_i32 do main di return exit_success do".to_owned(),
            "'two_fn' returns first_i32 second_i32, but this return has 1",
        ),
        (
            format!("{two}call two_fn with ret 0_i32 as x di info x do return exit_success do"),
            "'two_fn' returns first_i32 second_i32, which with ret names, as in with ret first_i32, not '0_i32'",
        ),
        (
            format!("{two}call two_fn with ret third_i32 di pass do return exit_success do"),
            "'two_fn' returns first_i32 second_i32, so it has no third_i32",
        ),
        (
            format!("{two}call two_fn return exit_success do"),
            "'two_fn' returns first_i32 second_i32, which call ... with ret takes for its block, as in call two_fn with ret first_i32 di ... do, or val takes one of, as in val value is two_fn with ret first_i32",
        ),
        (
            "main di str_parse \"7\"_str with ret ok_bool di pass do return exit_success do".to_owned(),
            "'str_parse' gives parsed_bool number_i32, so it has no ok_bool",
        ),
    ];
    let mut checked = Vec::new();
    for (source, expected) in &cases {
        checked.push((source.as_str(), *expected));
    }
    assert_diagnostics(&lega, &checked);
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

// Defs that gfx_frame calls draw in 2D, in 3D in the block of a camera, and
// with a camera of their own.
#[test]
fn gfx_draw_def() {
    let (result, scenes) = run_gfx("test/gfx_draw_def.bosonoga", vec![Input::default(); 2]);
    assert_eq!(result, Ok(ExitStatus::Success));
    let (cube, grid) = (0x00E0_7A5F, 0x005A_5A5A);
    let bar = |y: f32| Shape::Rectangle {
        x: 0.0,
        y,
        width: 480.0,
        height: 10.0,
        color: grid,
    };
    let box_at = |x: f32| {
        Shape::Cube(Cube {
            center: [x, 0.0, 0.0],
            size: [1.0, 1.0, 1.0],
            color: cube,
        })
    };
    let expected = [
        bar(0.0),
        Shape::Begin3d(Camera {
            position: [0.0, 2.0, 5.0],
            target: [0.0, 0.0, 0.0],
            up: [0.0, 1.0, 0.0],
            fov_degrees: 60.0,
        }),
        box_at(0.0),
        box_at(1.0),
        Shape::End3d,
        bar(520.0),
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

// fps_max of the group gfx is optional and 60 when left out, must be a
// positive number, and the group gfx has no other keys than width, height
// and fps_max.
#[test]
fn gfx_fps_max() {
    let parse = |gfx: &str| {
        let text = format!("[meta]\nname = \"t\"\nversion = \"0\"\n\n[gfx]\n{gfx}");
        Settings::parse("regina.ini", Path::new(""), &text)
    };
    let given = parse("width = 9\nheight = 9\nfps_max = 144\n").unwrap();
    assert_eq!(given.fps_max, 144);
    let left_out = parse("width = 9\nheight = 9\n").unwrap();
    assert_eq!(left_out.fps_max, bosonoga::setting::DEFAULT_FPS_MAX);
    assert_eq!(
        parse("width = 9\nheight = 9\nfps_max = 0\n").unwrap_err(),
        "regina.ini: fps_max must be positive"
    );
    assert_eq!(
        parse("width = 9\nheight = 9\nfps = 144\n").unwrap_err(),
        "regina.ini: the group gfx has only the keys width, height and fps_max, not fps"
    );
}
