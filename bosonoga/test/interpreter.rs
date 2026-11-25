use std::path::Path;

use bosonoga::ast::ExitStatus;
use bosonoga::gfx::{Input, KeyEvent, Scene, Window};
use bosonoga::setting::Settings;

#[cfg(test)]
fn check_program(path: &str, source: &str) {
    let program = match bosonoga::parse(source) {
        Ok(program) => program,
        Err(diagnostic) => panic!("{}", diagnostic.located(path, source)),
    };
    let status = match bosonoga::run(&program) {
        Ok((_, status)) => status,
        Err((_, diagnostic)) => panic!("{}", diagnostic.located(path, source)),
    };
    assert_eq!(status, ExitStatus::Success, "{path}");
}

#[test]
fn arithmetic() {
    check_program(
        "test/arithmetic.bosonoga",
        include_str!("arithmetic.bosonoga"),
    );
}

#[test]
fn functions() {
    check_program("test/function.bosonoga", include_str!("function.bosonoga"));
}

#[test]
fn var_arrays() {
    check_program(
        "test/var_array.bosonoga",
        include_str!("var_array.bosonoga"),
    );
}

#[test]
fn calls() {
    let source = include_str!("call.bosonoga");
    let program = match bosonoga::parse(source) {
        Ok(program) => program,
        Err(diagnostic) => panic!("{}", diagnostic.located("test/call.bosonoga", source)),
    };
    let (output, status) = match bosonoga::run(&program) {
        Ok(ran) => ran,
        Err((_, diagnostic)) => panic!("{}", diagnostic.located("test/call.bosonoga", source)),
    };
    assert_eq!(status, ExitStatus::Success);
    assert_eq!(output, "hello\ncountdown 2\ncountdown 1\ncountdown 0\n");
}

#[cfg(test)]
fn diagnostic(source: &str) -> String {
    match bosonoga::parse(source) {
        Ok(_) => panic!("expected a diagnostic for:\n{source}"),
        Err(diagnostic) => diagnostic.located("test", source),
    }
}

#[test]
fn from_global() {
    let source = include_str!("from_global.bosonoga");
    let path = "test/from_global.bosonoga";
    let program = match bosonoga::parse(source) {
        Ok(program) => program,
        Err(diagnostic) => panic!("{}", diagnostic.located(path, source)),
    };
    let (output, status) = match bosonoga::run(&program) {
        Ok(ran) => ran,
        Err((_, diagnostic)) => panic!("{}", diagnostic.located(path, source)),
    };
    assert_eq!(status, ExitStatus::Success);
    assert_eq!(output, "added 11 as 1\nadded 22 as 2\n");
}

#[test]
fn optionals() {
    let source = include_str!("optional.bosonoga");
    let path = "test/optional.bosonoga";
    let program = match bosonoga::parse(source) {
        Ok(program) => program,
        Err(diagnostic) => panic!("{}", diagnostic.located(path, source)),
    };
    let (output, status) = match bosonoga::run(&program) {
        Ok(ran) => ran,
        Err((_, diagnostic)) => panic!("{}", diagnostic.located(path, source)),
    };
    assert_eq!(output, "seven 7\nonly seven\nfirst\nbest 9\n");
    assert_eq!(status, ExitStatus::Success);
}

#[cfg(test)]
fn assert_diagnostics(cases: &[(String, &str)]) {
    let mut failures = Vec::new();
    for (source, expected) in cases {
        let got = diagnostic(source);
        if !got.starts_with(expected) {
            failures.push(format!("got:      {got}\nexpected: {expected}"));
        }
    }
    assert!(failures.is_empty(), "{}", failures.join("\n"));
}

// Every path of a def with values, and of a main block, ends in a return,
// and nothing follows a statement that always returns.
#[test]
fn return_diagnostics() {
    let main = "bosonoga_main\ndi\n  val one is f_fn of 1_i32 with ret 0_i32\n  info one\n  return exit_success\ndo\n";
    let def = |body: &str| format!("def f_fn n_i32 ret 0_i32\ndi\n{body}\ndo\n{main}");
    let cases = [
        (
            def("  if n_i32 gt 0_i32\n  di\n    return 1_i32\n  do"),
            "test:1:5: 'f_fn' returns 0_i32, but can reach its end without a return",
        ),
        (
            def("  while n_i32 gt 0_i32\n  di\n    return 1_i32\n  do"),
            "test:1:5: 'f_fn' returns 0_i32, but can reach its end without a return",
        ),
        (
            def(
                "  if n_i32 gt 0_i32\n  di\n    return 1_i32\n  do\n  else if n_i32 lt 0_i32\n  di\n    return -1_i32\n  do",
            ),
            "test:1:5: 'f_fn' returns 0_i32, but can reach its end without a return",
        ),
        (
            def("  return 1_i32\n  info n_i32"),
            "test:4:3: this is never reached, as the statement before it always returns",
        ),
        (
            def(
                "  if n_i32 gt 0_i32\n  di\n    return 1_i32\n  do\n  else\n  di\n    return 0_i32\n  do\n  nihil",
            ),
            "test:11:3: this is never reached, as the statement before it always returns",
        ),
        (
            def(
                "  str_parse \"1\"_str with ret 1_i32 as parsed\n  di\n    return parsed\n  do\n  return n_i32",
            ),
            "test:7:3: this is never reached, as the statement before it always returns",
        ),
        (
            "bosonoga_main\ndi\n  info 1_i32\ndo\n".to_owned(),
            "test:1:1: bosonoga_main can reach its end without a return, which gives exit_success or exit_failure",
        ),
        (
            def("  set n_i32 to_add 1_i32\n  return n_i32"),
            "test:3:7: 'n_i32' is a parameter, which is a val and cannot be set, but a copy can, as in var n is n_i32",
        ),
        (
            "def f_fn count_optional_i32 ret_void\ndi\n  set count_optional_i32 to nihil_i32\ndo\n"
                .to_owned(),
            "test:3:7: 'count_optional_i32' is a parameter, which is a val and cannot be set, but a copy can, as in var count is count_optional_i32",
        ),
    ];
    assert_diagnostics(&cases);
}

#[cfg(test)]
struct Frames(usize);

impl Window for Frames {
    fn info(&mut self, _text: &str) {}

    fn show(&mut self, _scene: &Scene) -> Option<Input> {
        self.0 += 1;
        if self.0 > 1000 {
            return None;
        }
        Some(Input {
            delta_time: 1.0 / 60.0,
            ..Input::default()
        })
    }
}

#[cfg(test)]
fn gfx_result(source: &str) -> Result<ExitStatus, String> {
    let program = match bosonoga::parse(source) {
        Ok(program) => program,
        Err(diagnostic) => panic!("{}", diagnostic.located("test", source)),
    };
    bosonoga::gfx::run(&program, &mut Frames(0))
}

// A section of a gfx program runs at most 100000 statements before the next
// frame, which gfx_main does not reach without gfx_await_next_frame.
#[test]
fn step_limit() {
    let looping = |steps: &str, wait: &str| {
        format!(
            "gfx_main\ndi\n  var i is 0_i32\n  while i lt {steps}\n  di\n    set i to_add 1_i32\n{wait}  do\n  return exit_success\ndo\n"
        )
    };
    let too_long = gfx_result(&looping("200000_i32", ""));
    assert!(
        too_long.as_ref().is_err_and(|message| message
            .ends_with(":4:3: gfx_main ran 100000 statements without gfx_await_next_frame")),
        "{too_long:?}"
    );
    let waiting = gfx_result(&looping("500_i32", "    gfx_await_next_frame\n"));
    assert_eq!(waiting, Ok(ExitStatus::Success));
    let frame = gfx_result(
        "gfx_frame\ndi\n  var i is 0_i32\n  while i lt 200000_i32\n  di\n    set i to_add 1_i32\n  do\ndo\n\ngfx_main\ndi\n  gfx_await_next_frame\n  return exit_success\ndo\n",
    );
    assert!(
        frame.as_ref().is_err_and(|message| message.ends_with(
            ":4:3: gfx_frame ran 100000 statements, the most that a section of a gfx program runs before the next frame"
        )),
        "{frame:?}"
    );
}

// Keeps the scenes it is shown.
#[cfg(test)]
struct Scenes(Vec<Scene>);

impl Window for Scenes {
    fn info(&mut self, _text: &str) {}

    fn show(&mut self, scene: &Scene) -> Option<Input> {
        self.0.push(scene.clone());
        if self.0.len() > 100 {
            return None;
        }
        Some(Input {
            delta_time: 1.0 / 60.0,
            ..Input::default()
        })
    }
}

// Loads a program with colors, from a file of its own, as colors come from
// settings. Its messages name the file test.
#[cfg(test)]
fn load_3d(source: &str) -> Result<bosonoga::Program, String> {
    load_with(
        "[meta]\nname = \"cube\"\nversion = \"0.2.6\"\n[gfx]\nwidth = 960\nheight = 540\n[color]\nbackground = \"87CEEB\"\ncube = \"E07A5F\"\ngrid = \"5A5A5A\"\n",
        source,
    )
}

#[cfg(test)]
fn load_with(regina: &str, source: &str) -> Result<bosonoga::Program, String> {
    static LOADED: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
    let settings = Settings::parse("regina.ini", Path::new("."), regina)
        .unwrap_or_else(|message| panic!("{message}"));
    let number = LOADED.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    let path = std::env::temp_dir().join(format!("bosonoga_3d_{}_{number}", std::process::id()));
    std::fs::write(&path, source).unwrap();
    let loaded = bosonoga::load(&path, &settings);
    std::fs::remove_file(&path).unwrap();
    let shown = path.display().to_string();
    match loaded {
        Ok(program) => Ok(program),
        Err(message) => Err(message.replacen(&shown, "test", 1)),
    }
}

#[cfg(test)]
fn run_3d(source: &str) -> (Result<ExitStatus, String>, Vec<Scene>) {
    let program = load_3d(source).unwrap_or_else(|message| panic!("{message}"));
    let mut window = Scenes(Vec::new());
    let result = bosonoga::gfx::run(&program, &mut window);
    let path = program.path().to_owned();
    (
        result.map_err(|message| message.replacen(&path, "test", 1)),
        window.0,
    )
}

// Shows four frames: the first before gfx_main, then one per wait.
#[cfg(test)]
fn frame_program(frame: &str) -> String {
    format!(
        "gfx_frame\ndi\n{frame}\ndo\n\ngfx_main\ndi\n  gfx_await_next_frame\n  gfx_await_next_frame\n  gfx_await_next_frame\n  return exit_success\ndo\n"
    )
}

#[cfg(test)]
const CAMERA_3D: &str = "gfx_camera_3d 6d0_f32 4d0_f32 6d0_f32 0d0_f32 1d0_f32 0d0_f32 0d0_f32 1d0_f32 0d0_f32 45d0_f32";

#[cfg(test)]
const CUBE: &str = "gfx_draw_cube 0d0_f32 1d0_f32 0d0_f32 2d0_f32 2d0_f32 2d0_f32 color_cube";

// A 2D rectangle, then a camera block of a grid and a cube, then 2D again,
// in that order, in every frame.
#[test]
fn camera_3d_scene() {
    use bosonoga::gfx::{Camera, Cube, Shape};
    let (result, shown) = run_3d(&frame_program(&format!(
        "  gfx_draw_rectangle 0d0_f32 0d0_f32 10d0_f32 10d0_f32 color_grid\n  {CAMERA_3D}\n  di\n    gfx_draw_grid 20_u32 1d0_f32 color_cube color_grid\n    {CUBE}\n    gfx_draw_cube_wires 0d0_f32 1d0_f32 0d0_f32 2d01_f32 2d01_f32 2d01_f32 color_grid\n  do\n  gfx_draw_circle 5d0_f32 5d0_f32 3d0_f32 color_cube"
    )));
    assert_eq!(result, Ok(ExitStatus::Success));
    assert_eq!(shown.len(), 4);
    let (cube, grid) = (0x00E0_7A5F, 0x005A_5A5A);
    for scene in &shown {
        assert_eq!(scene.clear, 0x0087_CEEB);
        assert_eq!(
            scene.shapes,
            vec![
                Shape::Rectangle {
                    x: 0.0,
                    y: 0.0,
                    width: 10.0,
                    height: 10.0,
                    color: grid,
                },
                Shape::Begin3d(Camera {
                    position: [6.0, 4.0, 6.0],
                    target: [0.0, 1.0, 0.0],
                    up: [0.0, 1.0, 0.0],
                    fov_degrees: 45.0,
                }),
                Shape::Grid {
                    slices: 20,
                    spacing: 1.0,
                    axes_color: cube,
                    color: grid,
                },
                Shape::Cube(Cube {
                    center: [0.0, 1.0, 0.0],
                    size: [2.0, 2.0, 2.0],
                    color: cube,
                }),
                Shape::CubeWires(Cube {
                    center: [0.0, 1.0, 0.0],
                    size: [2.01, 2.01, 2.01],
                    color: grid,
                }),
                Shape::End3d,
                Shape::Circle {
                    x: 5.0,
                    y: 5.0,
                    radius: 3.0,
                    color: cube,
                },
            ]
        );
    }
}

// A window that captures the mouse as each scene asks, as the host does,
// and gives the keys and mouse moves of its steps, one step per frame. A
// step can lose focus, which releases the mouse.
#[cfg(test)]
struct Steps {
    steps: Vec<(Vec<KeyEvent>, [f32; 2], bool)>,
    scenes: Vec<Scene>,
}

impl Window for Steps {
    fn info(&mut self, _text: &str) {}

    fn show(&mut self, scene: &Scene) -> Option<Input> {
        self.scenes.push(scene.clone());
        let (keys, mouse_move, focus_lost) = self.steps.get(self.scenes.len() - 1)?.clone();
        Some(Input {
            keys,
            delta_time: 1.0 / 60.0,
            mouse_move,
            mouse_captured: scene.mouse_captured && !focus_lost,
        })
    }
}

#[cfg(test)]
const INPUT_REGINA: &str = "[meta]\nname = \"input\"\nversion = \"0.2.7\"\n[gfx]\nwidth = 960\nheight = 540\n[color]\nbackground = \"87CEEB\"\n[key]\nwalk = \"SAPP_KEYCODE_W\"\nmouse = \"SAPP_KEYCODE_M\"\n";

#[cfg(test)]
const INPUT_PROGRAM: &str = r"bosonoga_global
di
  var held is false_bool
  var downs is 0_i32
  var ups is 0_i32
  var turned is 0d0_f32
  var tilted is 0d0_f32
  var captured_frames is 0_i32
do

gfx_key_down
di
  if gfx_key eq key_walk
  di
    set held to true_bool
    set downs to_add 1_i32
  do
  else if gfx_key eq key_mouse
  di
    if gfx_mouse_captured
    di
      gfx_mouse_release
    do
    else
    di
      gfx_mouse_capture
    do
  do
do

gfx_key_up
di
  if gfx_key eq key_walk
  di
    set held to false_bool
    set ups to_add 1_i32
  do
do

gfx_frame
di
  set turned to_add gfx_mouse_move_x
  set tilted to_add gfx_mouse_move_y
  if gfx_mouse_captured
  di
    set captured_frames to_add 1_i32
  do
do

gfx_main
di
  gfx_await_next_frame
  gfx_await_next_frame
  gfx_await_next_frame
  gfx_await_next_frame
  gfx_await_next_frame
  if not held and downs eq 2_i32 and ups eq 2_i32 and turned eq_3 3d0_f32 and tilted eq_3 -4d0_f32 and captured_frames eq 3_i32
  di
    return exit_success
  do
  return exit_failure
do
";

#[cfg(test)]
fn input_steps() -> Steps {
    let key = |name: &str| bosonoga::key::named(name).unwrap();
    let (w, m) = (key("SAPP_KEYCODE_W"), key("SAPP_KEYCODE_M"));
    Steps {
        steps: vec![
            (vec![KeyEvent::Down(w)], [0.0, 0.0], false),
            (vec![KeyEvent::Down(m)], [0.0, 0.0], false),
            (
                vec![KeyEvent::Up(w), KeyEvent::Down(w), KeyEvent::Up(w)],
                [3.0, -4.0],
                false,
            ),
            (Vec::new(), [0.0, 0.0], true),
            (vec![KeyEvent::Down(m)], [0.0, 0.0], false),
            (Vec::new(), [0.0, 0.0], false),
        ],
        scenes: Vec::new(),
    }
}

#[test]
fn key_up_and_mouse() {
    let program =
        load_with(INPUT_REGINA, INPUT_PROGRAM).unwrap_or_else(|message| panic!("{message}"));
    let mut window = input_steps();
    assert_eq!(
        bosonoga::gfx::run(&program, &mut window),
        Ok(ExitStatus::Success)
    );
    let captured: Vec<bool> = window
        .scenes
        .iter()
        .map(|scene| scene.mouse_captured)
        .collect();
    assert_eq!(captured, [false, false, true, true, false, true]);
}

#[test]
fn akas() {
    let program = bosonoga::load(Path::new("test/aka.bosonoga"), &Settings::default())
        .unwrap_or_else(|message| panic!("{message}"));
    let (output, status) = match bosonoga::run(&program) {
        Ok(ran) => ran,
        Err((_, diagnostic)) => panic!("{}", program.located(&diagnostic)),
    };
    assert_eq!(output, "countdown 1\ncountdown 0\n");
    assert_eq!(status, ExitStatus::Success);
}
