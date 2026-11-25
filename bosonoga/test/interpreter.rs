use std::path::Path;

use bosonoga::ast::ExitStatus;
use bosonoga::gfx::{Input, KeyEvent, Scene, Window};
use bosonoga::setting::Settings;

#[test]
fn startup_version_check_setting() {
    let meta = "[meta]\nname = \"example\"\nversion = \"0.2.5\"\n";
    for (suffix, expected) in [
        ("", false),
        ("skip_version_check = false\n", false),
        ("skip_version_check = true  \n", true),
    ] {
        let settings = Settings::parse("regina.ini", Path::new("."), &format!("{meta}{suffix}"))
            .unwrap_or_else(|message| panic!("{message}"));
        assert_eq!(settings.skip_version_check, expected);
    }
    let invalid = Settings::parse(
        "regina.ini",
        Path::new("."),
        &format!("{meta}skip_version_check = yes\n"),
    );
    assert_eq!(
        invalid.unwrap_err(),
        "regina.ini: skip_version_check must be true or false"
    );
}

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
fn shift() {
    check_program("test/shift.bosonoga", include_str!("shift.bosonoga"));
}

#[test]
fn loops() {
    check_program("test/loop.bosonoga", include_str!("loop.bosonoga"));
}

#[test]
fn functions() {
    check_program("test/function.bosonoga", include_str!("function.bosonoga"));
}

#[test]
fn arrays() {
    check_program("test/array.bosonoga", include_str!("array.bosonoga"));
}

#[test]
fn var_arrays() {
    check_program(
        "test/var_array.bosonoga",
        include_str!("var_array.bosonoga"),
    );
}

#[test]
fn conversions() {
    check_program(
        "test/conversion.bosonoga",
        include_str!("conversion.bosonoga"),
    );
}

#[test]
fn modules() {
    let program = match bosonoga::load(Path::new("test/module.bosonoga"), &Settings::default()) {
        Ok(program) => program,
        Err(message) => panic!("{message}"),
    };
    let status = match bosonoga::run(&program) {
        Ok((_, status)) => status,
        Err((_, diagnostic)) => panic!("{}", program.located(&diagnostic)),
    };
    assert_eq!(status, ExitStatus::Success);
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
fn call_diagnostics() {
    let two = "def two_fn ret 0_bool 1_i32\ndi\n  return true_bool 1_i32\ndo\n";
    let one = "def one_fn ret 0_i32\ndi\n  return 1_i32\ndo\n";
    let none = "def none_fn n_i32 ret_void\ndi\n  info n_i32\ndo\n";
    let main = |body: &str| format!("bosonoga_main\ndi\n{body}\n  return exit_success\ndo\n");
    let cases = [
        (
            format!(
                "{one}{}",
                main("  call one_fn with ret 0_i32 as x di\n info x\n do")
            ),
            "test:7:3: 'one_fn' returns i32, which val, var or set ... to takes, as in val value is one_fn with ret 0_i32",
        ),
        (
            format!("{one}{}", main("  call one_fn")),
            "test:7:3: 'one_fn' returns i32, which val, var or set ... to takes",
        ),
        (
            format!("{two}{}", main("  call two_fn\n  info 1_i32")),
            "test:8:3: 'two_fn' returns 0_bool, 1_i32, which call ... with ret takes for its block, each named, as in call two_fn with ret 0_bool as value di ... do",
        ),
        (
            format!(
                "{none}{}",
                main("  call none_fn of 1_i32 with ret 0_i32 as x di\n info x\n do")
            ),
            "test:7:25: 'none_fn' returns no values, so its call has neither with ret nor a block",
        ),
        (
            format!(
                "{none}{}",
                main("  val x is none_fn of 1_i32 with ret 0_i32\n  info x")
            ),
            "test:7:12: 'none_fn' returns no values, so it is called with call, as in call none_fn of ...",
        ),
        (
            format!(
                "{two}{}",
                main("  val x is two_fn with ret 0_bool\n  info x")
            ),
            "test:7:12: 'two_fn' returns 0_bool, 1_i32, which call ... with ret takes for its block",
        ),
        (
            format!(
                "{two}{}",
                main("  call two_fn with ret 1_i32 as n 0_bool as b di\n info n b\n do")
            ),
            "test:7:35: the values are taken once each, by increasing index, so 0_bool cannot follow 1_i32",
        ),
        (
            format!(
                "{two}{}",
                main("  call two_fn with ret 0_bool as a 0_bool as b di\n info a b\n do")
            ),
            "test:7:36: the values are taken once each, by increasing index, so 0_bool cannot follow 0_bool",
        ),
        (
            format!(
                "{two}{}",
                main("  call two_fn with ret 0_i32 as n di\n info n\n do")
            ),
            "test:7:24: 'two_fn' returns 0_bool, 1_i32, so it has no 0_i32",
        ),
        (
            format!(
                "{two}{}",
                main("  call two_fn with ret 0_bool as b 1_i32 as n di\n info n\n do")
            ),
            "test:7:34: 'b' is never read",
        ),
        (
            main("  str_parse \"1\"_str with ret 1_i32 as n 0_bool as b di\n info n b\n do"),
            "test:3:41: the values are taken once each, by increasing index, so 0_bool cannot follow 1_i32",
        ),
        (
            main("  str_parse \"1\"_str with ret 1_str as n di\n info n\n do"),
            "test:3:30: 'str_parse' gives 0_bool, 1_i32, so it has no 1_str",
        ),
        (
            format!(
                "{two}{}",
                main("  call two_fn with ret 0_bool as b\n  info b")
            ),
            "test:8:3: expected another value to take, like 1_i32 as count, or di, got 'info'",
        ),
    ];
    let mut failures = Vec::new();
    for (source, expected) in cases {
        let got = diagnostic(&source);
        if !got.starts_with(expected) {
            failures.push(format!("got:      {got}\nexpected: {expected}"));
        }
    }
    assert!(failures.is_empty(), "{}", failures.join("\n"));
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
fn from_global_diagnostics() {
    let global = "bosonoga_global\ndi\n  var total is 0_i32\n  val step is 1_i32\ndo\n";
    let main = "bosonoga_main\ndi\n  val sum is f_fn with ret 0_i32\n  info sum total step\n  return exit_success\ndo\n";
    let def = |head: &str, body: &str| format!("def f_fn {head} ret 0_i32\ndi\n{body}\ndo\n");
    let cases = [
        (
            format!(
                "{global}{}{main}",
                def("with from_global total", "  return 1_i32")
            ),
            "test:6:27: 'total' is in with from_global, but the function never uses it",
        ),
        (
            format!(
                "{global}{}{main}",
                def("with from_global missing", "  return 1_i32")
            ),
            "test:6:27: 'missing' is not a variable of bosonoga_global",
        ),
        (
            format!(
                "{}{global}{main}",
                def("with from_global total", "  return total")
            ),
            "test:1:27: 'total' is not a variable of bosonoga_global, and with from_global needs bosonoga_global before the function",
        ),
        (
            format!(
                "{global}{}{main}",
                def("with from_global total total", "  return total")
            ),
            "test:6:33: 'total' is already in with from_global",
        ),
        (
            format!(
                "{global}{}{main}",
                def(
                    "with from_global step",
                    "  set step to 2_i32\n  return step"
                )
            ),
            "test:8:7: 'step' is a val and cannot be set",
        ),
        (
            format!("{global}{}{main}", def("", "  return total")),
            "test:8:10: unknown variable 'total'",
        ),
        (
            format!(
                "bosonoga_global\ndi\n  var log is 0_i32\ndo\n{}bosonoga_main\ndi\n  val one is f_fn with ret 0_i32\n  info one\n  return exit_success\ndo\n",
                def("with from_global log", "  set log to 1_i32\n  return 1_i32")
            ),
            "test:3:7: 'log' is never read",
        ),
    ];
    let mut failures = Vec::new();
    for (source, expected) in cases {
        let got = diagnostic(&source);
        if !got.starts_with(expected) {
            failures.push(format!("got:      {got}\nexpected: {expected}"));
        }
    }
    assert!(failures.is_empty(), "{}", failures.join("\n"));
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

#[cfg(test)]
fn optional_main(body: &str) -> String {
    format!(
        "def maybe_fn ret 0_optional_i32\ndi\n  return 1_optional_i32\ndo\nbosonoga_main\ndi\n  val count is maybe_fn with ret 0_optional_i32\n{body}\n  return exit_success\ndo\n"
    )
}

// How an optional is read, with if some, and what cannot read it.
#[test]
fn optional_read_diagnostics() {
    let main = optional_main;
    let cases = [
        (
            main("  info count"),
            "test:8:8: 'count' is optional_i32, which info cannot show, but its value can, as in if some count as value",
        ),
        (
            main("  var text is \"\"_str\n  set text to_join count"),
            "test:9:20: 'count' is optional_i32, which to_join cannot show",
        ),
        (
            main("  if count eq 1_i32 di\n nihil\n do"),
            "test:8:12: optional_i32 cannot be compared, but its value can, as in if some count as value",
        ),
        (
            main("  if count di\n nihil\n do"),
            "test:8:6: 'count' is optional_i32, which is tested with some, as in if some count or if some count as value",
        ),
        (
            main("  if some count as n and true_bool di\n info n\n do"),
            "test:8:6: some ... as ... names the value for the block of an if or else if, so it is the whole condition, without not, and or or",
        ),
        (
            main("  if not some count as n di\n info n\n do"),
            "test:8:10: some ... as ... names the value for the block of an if or else if",
        ),
        (
            main("  while some count as n di\n info n\n do"),
            "test:8:9: some ... as ... names the value for the block of an if or else if",
        ),
        (
            main("  val n is 1_i32\n  if some n di\n info count\n do"),
            "test:9:6: some tests an optional, like optional_i32, but 'n' is i32",
        ),
        (
            main("  if some count as n di\n nihil\n do"),
            "test:8:20: 'n' is never read",
        ),
        (
            main("  if some count as count di\n info count\n do"),
            "test:8:20: variable 'count' already exists",
        ),
        (
            main("  var other is count\n  set other to_add other 1_i32"),
            "test:9:13: 'other' is optional_i32, which cannot be used in arithmetic",
        ),
        (
            main("  var other is count\n  set other to 1_i64\n  info other"),
            "test:9:16: set ... to needs optional_i32, but '1_i64' is i64",
        ),
        (
            main("  if some count as n di\n info n\n do\n  val x is nihil_flags"),
            "test:11:12: nihil is followed by the type that is absent, i32, i64, u32, u64, f32, f64, bool, str or color, as in nihil_i32",
        ),
        (
            main("  val x is nihil_optional_i32\n  info count"),
            "test:8:12: nihil is always optional, so it is written nihil_i32, not nihil_optional_i32",
        ),
        (
            main("  val nihil_count is 1_i32\n  info nihil_count count"),
            "test:8:7: nihil is followed by the type that is absent",
        ),
        (
            "def f_fn ret 0_i32\ndi\n  return nihil_i32\ndo\n".to_owned(),
            "test:3:10: return needs i32, but 'nihil_i32' is optional_i32, as nihil_i32 is an absent optional_i32",
        ),
        (
            main("  var rows is array 0_optional_i32 with_len_col 2_i32\n  info count"),
            "test:8:21: an array cannot have optional_i32, but a variable can",
        ),
        (
            "def f_fn ret 0_optional_optional_i32\ndi\n  return 1_i32\ndo\n".to_owned(),
            "test:1:14: expected optional_ and i32, i64, u32, u64, f32, f64, bool, str or color, as in 0_optional_i32 or count_optional_i32, got '0_optional_optional_i32'",
        ),
        (
            "def f_fn count_optional_flags ret_void\ndi\n  nihil\ndo\n".to_owned(),
            "test:1:10: expected optional_ and i32, i64, u32, u64, f32, f64, bool, str or color",
        ),
    ];
    assert_diagnostics(&cases);
}

// How an optional is made: a present literal, none or to_optional_i32.
#[test]
fn optional_make_diagnostics() {
    let main = optional_main;
    let cases = [
        (
            "def f_fn ret 0_optional_i32\ndi\n  return 1_i32\ndo\n".to_owned(),
            "test:3:10: return needs optional_i32, but '1_i32' is i32, so write 1_optional_i32",
        ),
        (
            "def f_fn n_i32 ret 0_optional_i32\ndi\n  return n_i32\ndo\n".to_owned(),
            "test:3:10: return needs optional_i32, but 'n_i32' is i32, so convert it first, as in val present is to_optional_i32 n_i32",
        ),
        (
            "def f_fn n_optional_str ret_void\ndi\n  if some n_optional_str as n di\n info n\n do\ndo\nbosonoga_main\ndi\n  call f_fn of \"x\"_str\n  return exit_success\ndo\n".to_owned(),
            "test:9:16: f_fn needs optional_str, but '\"x\"_str' is str, so write \"x\"_optional_str",
        ),
        (
            main("  var other is count\n  val n is 2_i32\n  set other to n\n  info n"),
            "test:10:16: 'other' is optional_i32, but 'n' is i32, so convert it, as in set other to_optional_i32 n",
        ),
        (
            main("  var other is count\n  set other to 2_i32"),
            "test:9:16: set ... to needs optional_i32, but '2_i32' is i32, so write 2_optional_i32",
        ),
        (
            format!(
                "def one_fn ret 0_i32\ndi\n  return 1_i32\ndo\n{}",
                main("  var other is count\n  set other to one_fn with ret 0_i32")
            ),
            "test:13:7: 'other' is optional_i32, but 'one_fn' returns i32, so take it first, as in val found is one_fn ... with ret 0_i32, then set other to_optional_i32 found",
        ),
        (
            main("  val other is to_optional_i32 7_i32"),
            "test:8:32: a literal needs no to_optional_i32, write 7_optional_i32",
        ),
        (
            main("  val wide is 7_i64\n  val other is to_optional_i32 wide"),
            "test:9:32: to_optional_i32 needs i32, but 'wide' is i64",
        ),
        (
            main("  val other is to_optional_i32 count"),
            "test:8:32: to_optional_i32 needs i32, but 'count' is optional_i32",
        ),
        (
            main("  val other is \"x\"_optional_text"),
            "test:8:16: a string must have the suffix _str, as in \"text\"_str, or _optional_str",
        ),
        (
            main("  val other is 3000000000_optional_i32"),
            "test:8:16: the number is outside the i32 range",
        ),
        (
            main("  val other is 1d5_optional_i32"),
            "test:8:16: an integer cannot have a fraction",
        ),
        (
            main("  val some is 1_i32"),
            "test:8:7: expected a variable name, got 'some'",
        ),
    ];
    assert_diagnostics(&cases);
}

// An operation starts from its variable, which is not repeated.
#[test]
fn arithmetic_diagnostics() {
    let main = |body: &str| {
        format!(
            "bosonoga_main\ndi\n  var count is 1_i32\n{body}\n  info count\n  return exit_success\ndo\n"
        )
    };
    let cases = [
        (
            main("  set count to_add count 1_i32"),
            "test:4:20: set count to_add starts from 'count', so 'count' is not repeated, as in set count to_add 1_i32",
        ),
        (
            main("  set count to_multiply 2_i32 count"),
            "test:4:31: set count to_multiply starts from 'count', so 'count' is not repeated",
        ),
        (main("  set count to_add"), "test:5:3: expected"),
    ];
    assert_diagnostics(&cases);
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

#[test]
fn every_path_returns() {
    check_program(
        "every_path_returns",
        "def sign_fn n_i32 ret 0_i32\ndi\n  if n_i32 gt 0_i32\n  di\n    return 1_i32\n  do\n  else if n_i32 lt 0_i32\n  di\n    return -1_i32\n  do\n  else\n  di\n    return 0_i32\n  do\ndo\n\nbosonoga_main\ndi\n  val minus is sign_fn of -5_i32 with ret 0_i32\n  if minus eq -1_i32\n  di\n    return exit_success\n  do\n  else\n  di\n    return exit_failure\n  do\ndo\n",
    );
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

// gfx_heap_bytes grows with a str that grows, and is read only in gfx_frame.
#[test]
fn heap_bytes() {
    let growing = gfx_result(
        "bosonoga_global\ndi\n  var first is 0_u64\n  var latest is 0_u64\n  var text is \"\"_str\ndo\n\ngfx_frame\ndi\n  if first eq 0_u64\n  di\n    set first to gfx_heap_bytes\n  do\n  set latest to gfx_heap_bytes\ndo\n\ngfx_main\ndi\n  gfx_await_next_frame\n  var i is 0_i32\n  while i lt 1000_i32\n  di\n    var longer is \"\"_str\n    set longer to_join text \"0123456789\"_str\n    set text to longer\n    set i to_add 1_i32\n  do\n  gfx_await_next_frame\n  gfx_await_next_frame\n  if first gt 0_u64 and latest ge first\n  di\n    var grown is latest\n    set grown to_subtract first\n    if grown ge 10000_u64\n    di\n      return exit_success\n    do\n  do\n  return exit_failure\ndo\n",
    );
    assert_eq!(growing, Ok(ExitStatus::Success));
    let outside = [
        (
            "gfx_main\ndi\n  val bytes is gfx_heap_bytes\n  info bytes\n  return exit_success\ndo\n",
            "test:3:16: gfx_heap_bytes can only be in gfx_frame",
        ),
        (
            "def f_fn ret 0_u64\ndi\n  return gfx_heap_bytes\ndo\n\ngfx_frame\ndi\n  val bytes is f_fn with ret 0_u64\n  info bytes\ndo\n\ngfx_main\ndi\n  return exit_success\ndo\n",
            "test:3:10: gfx_heap_bytes can only be in gfx_frame",
        ),
    ];
    for (source, expected) in outside {
        assert_eq!(diagnostic(source), expected);
    }
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

// A frame that skips the camera block draws in 2D only, and the next frame
// can have its camera again.
#[test]
fn camera_3d_per_frame() {
    use bosonoga::gfx::Shape;
    let (result, shown) = run_3d(&format!(
        "bosonoga_global\ndi\n  var frame is 0_i32\ndo\n\ngfx_frame\ndi\n  set frame to_add 1_i32\n  if not frame eq 2_i32\n  di\n    {CAMERA_3D}\n    di\n      {CUBE}\n    do\n  do\ndo\n\ngfx_main\ndi\n  gfx_await_next_frame\n  gfx_await_next_frame\n  gfx_await_next_frame\n  return exit_success\ndo\n"
    ));
    assert_eq!(result, Ok(ExitStatus::Success));
    let counts: Vec<usize> = shown.iter().map(|scene| scene.shapes.len()).collect();
    assert_eq!(counts, [3, 0, 3, 3]);
    assert!(matches!(shown[2].shapes[0], Shape::Begin3d(_)));
    assert_eq!(shown[2].shapes[2], Shape::End3d);
}

// 3D draws only in the block of gfx_camera_3d, 2D draws only outside it,
// and the camera only in gfx_frame, outside defs and other cameras.
#[test]
fn camera_3d_placement() {
    let cases = [
        (
            frame_program(&format!("  {CUBE}")),
            "test:3:3: gfx_draw_cube can only be in the block of gfx_camera_3d, in gfx_frame",
        ),
        (
            frame_program(&format!(
                "  {CAMERA_3D}\n  di\n    gfx_clear color_cube\n  do"
            )),
            "test:5:5: gfx_clear draws in 2D, so it goes before or after the block of gfx_camera_3d",
        ),
        (
            frame_program(&format!(
                "  {CAMERA_3D}\n  di\n    {CAMERA_3D}\n    di\n      {CUBE}\n    do\n  do"
            )),
            "test:5:5: gfx_camera_3d cannot be in the block of another gfx_camera_3d",
        ),
        (
            format!(
                "gfx_main\ndi\n  {CAMERA_3D}\n  di\n    {CUBE}\n  do\n  return exit_success\ndo\n"
            ),
            "test:3:3: gfx_camera_3d can only be in gfx_frame",
        ),
        (
            format!(
                "def scene_fn ret_void\ndi\n  {CAMERA_3D}\n  di\n    {CUBE}\n  do\ndo\n\n{}",
                frame_program("  call scene_fn")
            ),
            "test:3:3: gfx_camera_3d can only be in gfx_frame",
        ),
        (
            frame_program(&format!(
                "  {CAMERA_3D}\n  di\n    gfx_draw_grid 20_i32 1d0_f32 color_cube color_cube\n  do"
            )),
            "test:5:19: gfx_draw_grid needs u32, but '20_i32' is i32",
        ),
        (
            frame_program(
                &format!("  {CAMERA_3D}").replace("45d0_f32", "45_i32 di\n    nihil\n  do"),
            ),
            "test:3:89: gfx_camera_3d needs f32, but '45_i32' is i32",
        ),
    ];
    for (source, expected) in cases {
        let found = match load_3d(&source) {
            Ok(_) => String::from("no error"),
            Err(message) => message,
        };
        assert_eq!(found, expected, "{source}");
    }
}

// Values are checked when they are drawn, and errors name the statement.
#[test]
fn camera_3d_values() {
    let cases = [
        (
            frame_program(&format!(
                "  var size is 2d0_f32\n  set size to_subtract 2d0_f32\n  {CAMERA_3D}\n  di\n    gfx_draw_cube_wires 0d0_f32 1d0_f32 0d0_f32 2d0_f32 size 2d0_f32 color_cube\n  do"
            )),
            "test:7:5: gfx_draw_cube_wires needs a width, height and depth above 0, but its height is 0",
        ),
        (
            frame_program(&format!(
                "  {CAMERA_3D}\n  di\n    gfx_draw_grid 21_u32 1d0_f32 color_cube color_grid\n  do"
            )),
            "test:5:5: gfx_draw_grid needs an even slice count from 2 to 256, not 21",
        ),
        (
            frame_program(&format!("  {CAMERA_3D}").replace(
                "0d0_f32 1d0_f32 0d0_f32 45d0_f32",
                "0d0_f32 0d0_f32 0d0_f32 45d0_f32 di\n    nihil\n  do",
            )),
            "test:3:3: gfx_camera_3d needs an up that is not 0 0 0",
        ),
        (
            frame_program(&format!(
                "  var i is 0_i32\n  while i lt 2_i32\n  di\n    {CAMERA_3D}\n    di\n      {CUBE}\n    do\n    set i to_add 1_i32\n  do"
            )),
            "test:6:5: gfx_camera_3d runs at most once per frame",
        ),
    ];
    for (source, expected) in cases {
        let (result, _) = run_3d(&source);
        assert_eq!(result, Err(expected.to_owned()), "{source}");
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
fn mouse_settings() {
    use bosonoga::setting::Mouse;
    let input_regina = INPUT_REGINA.replace("0.2.7", "0.2.9");
    let parse = |group: &str| {
        Settings::parse(
            "regina.ini",
            Path::new("."),
            &format!("{input_regina}{group}"),
        )
    };
    let default = Mouse {
        sensitivity: 1.0,
        invert_north: false,
        invert_west: false,
    };
    assert_eq!(Mouse::default(), default);
    assert_eq!(parse("").map(|settings| settings.mouse), Ok(default));
    assert_eq!(
        parse("[mouse]\n").map(|settings| settings.mouse),
        Ok(default)
    );
    assert_eq!(
        parse("[mouse]\ninvert_west = true_bool\n").map(|settings| settings.mouse),
        Ok(Mouse {
            invert_west: true,
            ..default
        })
    );
    assert_eq!(
        parse("[mouse]\ninvert_north = true_bool\n")
            .map(|settings| settings.mouse.applied([3.0, -4.0])),
        Ok([3.0, 4.0])
    );
    assert_eq!(
        parse("[mouse]\ninvert_west = true_bool\n")
            .map(|settings| settings.mouse.applied([3.0, -4.0])),
        Ok([-3.0, -4.0])
    );
    let number = "sensitivity must be one f32 number above 0, like 1d0_f32";
    let errors = [
        ("[mouse]\nsensitivity = 0d0_f32\n", number),
        ("[mouse]\nsensitivity = -1d0_f32\n", number),
        ("[mouse]\nsensitivity = 1_i32\n", number),
        ("[mouse]\nsensitivity = 1d0_f64\n", number),
        ("[mouse]\nsensitivity = 1d0_f32 2d0_f32\n", number),
        ("[mouse]\nsensitivity = \"1d0_f32\"\n", number),
        (
            "[mouse]\ninvert_north = true\n",
            "invert_north must be true_bool or false_bool",
        ),
        (
            "[mouse]\ninvert_west = 1_i32\n",
            "invert_west must be true_bool or false_bool",
        ),
        (
            "[mouse]\ninvert_west = \"false_bool\"\n",
            "invert_west must be true_bool or false_bool",
        ),
        (
            "[mouse]\nsensitivity = 1d0_f32\nspeed = 1d0_f32\n",
            "the group mouse has only the keys sensitivity, invert_north and invert_west, not speed",
        ),
    ];
    for (group, expected) in errors {
        assert_eq!(
            parse(group).err(),
            Some(format!("regina.ini: {expected}")),
            "{group}"
        );
    }
    let no_gfx =
        "[meta]\nname = \"input\"\nversion = \"0.2.9\"\n[mouse]\ninvert_west = false_bool\n";
    assert_eq!(
        Settings::parse("regina.ini", Path::new("."), no_gfx).err(),
        Some("regina.ini: the group mouse needs the group gfx".to_owned())
    );
}

#[test]
fn mouse_settings_move() {
    let regina = format!(
        "{}[mouse]\nsensitivity = 0d5_f32\ninvert_north = true_bool\ninvert_west = true_bool\n",
        INPUT_REGINA.replace("0.2.7", "0.2.9")
    );
    let changed = INPUT_PROGRAM.replace(
        "turned eq_3 3d0_f32 and tilted eq_3 -4d0_f32",
        "turned eq_3 -1d5_f32 and tilted eq_3 2d0_f32",
    );
    let program = load_with(&regina, &changed).unwrap_or_else(|message| panic!("{message}"));
    let mut window = input_steps();
    assert_eq!(
        bosonoga::gfx::run(&program, &mut window),
        Ok(ExitStatus::Success)
    );
}

#[test]
fn mouse_capture_twice() {
    let cases = [
        (
            "gfx_frame\ndi\n  gfx_mouse_capture\ndo\n\ngfx_main\ndi\n  gfx_await_next_frame\n  return exit_success\ndo\n",
            "test:3:3: gfx_mouse_capture needs a released mouse, but the mouse is captured",
        ),
        (
            "gfx_frame\ndi\n  gfx_mouse_release\ndo\n\ngfx_main\ndi\n  return exit_success\ndo\n",
            "test:3:3: gfx_mouse_release needs a captured mouse, but the mouse is released",
        ),
    ];
    for (source, expected) in cases {
        let program = load_with(INPUT_REGINA, source).unwrap_or_else(|message| panic!("{message}"));
        let path = program.path().to_owned();
        let mut window = Steps {
            steps: vec![(Vec::new(), [0.0, 0.0], false); 4],
            scenes: Vec::new(),
        };
        let result = bosonoga::gfx::run(&program, &mut window);
        assert_eq!(
            result.map_err(|message| message.replacen(&path, "test", 1)),
            Err(expected.to_owned())
        );
    }
}

#[test]
fn input_placement() {
    let cases = [
        (
            "gfx_frame\ndi\n  info gfx_key\ndo\n\ngfx_main\ndi\n  return exit_success\ndo\n",
            "test:3:8: gfx_key can only be in gfx_key_down or gfx_key_up",
        ),
        (
            "gfx_key_down\ndi\n  info gfx_mouse_move_x\ndo\n\ngfx_main\ndi\n  return exit_success\ndo\n",
            "test:3:8: gfx_mouse_move_x can only be in gfx_frame",
        ),
        (
            "gfx_main\ndi\n  info gfx_mouse_move_y\n  return exit_success\ndo\n",
            "test:3:8: gfx_mouse_move_y can only be in gfx_frame",
        ),
        (
            "gfx_main\ndi\n  info gfx_mouse_captured\n  return exit_success\ndo\n",
            "test:3:8: gfx_mouse_captured can only be in gfx_frame, gfx_key_down or gfx_key_up",
        ),
        (
            "gfx_main\ndi\n  gfx_mouse_capture\n  return exit_success\ndo\n",
            "test:3:3: gfx_mouse_capture can only be in gfx_frame, gfx_key_down or gfx_key_up",
        ),
        (
            "def let_go_fn ret_void\ndi\n  gfx_mouse_release\ndo\n\ngfx_key_up\ndi\n  call let_go_fn\ndo\n\ngfx_main\ndi\n  return exit_success\ndo\n",
            "test:3:3: gfx_mouse_release can only be in gfx_frame, gfx_key_down or gfx_key_up",
        ),
        (
            "gfx_key_up\ndi\n  info gfx_key\ndo\n\ngfx_key_up\ndi\n  info gfx_key\ndo\n\ngfx_main\ndi\n  return exit_success\ndo\n",
            "test:6:1: there is already a gfx_key_up block",
        ),
    ];
    for (source, expected) in cases {
        assert_eq!(diagnostic(source), expected);
    }
}

#[cfg(test)]
struct Infos(String, Vec<Scene>);

impl Window for Infos {
    fn info(&mut self, text: &str) {
        self.0.push_str(text);
    }

    fn show(&mut self, scene: &Scene) -> Option<Input> {
        self.1.push(scene.clone());
        Some(Input {
            delta_time: 1.0 / 60.0,
            ..Input::default()
        })
    }
}

#[cfg(test)]
const ALPHA_REGINA: &str = "[meta]\nname = \"alpha\"\nversion = \"0.2.8\"\n[gfx]\nwidth = 960\nheight = 540\n[color]\nbackground = \"87CEEB\"\nsolid = \"1A212E\"\nshade = \"1A212ED1\"\n";

// RRGGBBAA in regina.ini is a translucent color, kept as 0xTTRRGGBB, where
// TT is 255 less the alpha, so RRGGBB colors stay as they were.
#[test]
fn alpha_settings() {
    let settings = Settings::parse("regina.ini", Path::new("."), ALPHA_REGINA)
        .unwrap_or_else(|message| panic!("{message}"));
    assert_eq!(settings.color("solid"), Some(0x001A_212E));
    assert_eq!(settings.color("shade"), Some(0x2E1A_212E));
    assert_eq!(bosonoga::gfx::alpha(0x2E1A_212E), 0xD1);
    assert!(bosonoga::gfx::is_opaque(0) && !bosonoga::gfx::is_opaque(0x2E1A_212E));
    let errors = [
        (
            "shade = \"1A212EFF\"",
            "shade has an alpha of FF, which is opaque, so it is written as RRGGBB: 1A212E",
        ),
        (
            "shade = \"1A212ED\"",
            "shade must be 6 uppercase hex digits, RRGGBB, or 8 with an alpha, RRGGBBAA",
        ),
        (
            "shade = \"1a212ed1\"",
            "shade must be 6 uppercase hex digits, RRGGBB, or 8 with an alpha, RRGGBBAA",
        ),
    ];
    for (line, expected) in errors {
        let regina = ALPHA_REGINA.replace("shade = \"1A212ED1\"", line);
        assert_eq!(
            Settings::parse("regina.ini", Path::new("."), &regina).err(),
            Some(format!("regina.ini: {expected}"))
        );
    }
    let regina = ALPHA_REGINA.replace("background = \"87CEEB\"", "background = \"87CEEB80\"");
    assert_eq!(
        Settings::parse("regina.ini", Path::new("."), &regina).err(),
        Some("regina.ini: background must be opaque, as RRGGBB".to_owned())
    );
}

// A translucent color draws in 2D and shows as RRGGBBAA, and gfx_clear and
// every 3D draw need an opaque one.
#[test]
fn alpha_draws() {
    use bosonoga::gfx::Shape;
    let program = load_with(
        ALPHA_REGINA,
        &frame_program(
            "  gfx_draw_rectangle 0d0_f32 0d0_f32 10d0_f32 10d0_f32 color_shade\n  info color_shade \" \"_str color_solid",
        ),
    )
    .unwrap_or_else(|message| panic!("{message}"));
    let mut window = Infos(String::new(), Vec::new());
    assert_eq!(
        bosonoga::gfx::run(&program, &mut window),
        Ok(ExitStatus::Success)
    );
    assert!(window.0.starts_with("1A212ED1 1A212E\n"), "{}", window.0);
    assert_eq!(
        window.1[0].shapes,
        vec![Shape::Rectangle {
            x: 0.0,
            y: 0.0,
            width: 10.0,
            height: 10.0,
            color: 0x2E1A_212E,
        }]
    );
    let camera = "gfx_camera_3d 6d0_f32 4d0_f32 6d0_f32 0d0_f32 1d0_f32 0d0_f32 0d0_f32 1d0_f32 0d0_f32 45d0_f32";
    let cases = [
        (
            "  gfx_clear color_shade".to_owned(),
            "test:3:3: gfx_clear needs an opaque color, as RRGGBB, but its color is 1A212ED1",
        ),
        (
            format!(
                "  {camera}\n  di\n    gfx_draw_cube 0d0_f32 1d0_f32 0d0_f32 2d0_f32 2d0_f32 2d0_f32 color_shade\n  do"
            ),
            "test:5:5: gfx_draw_cube needs an opaque color, as RRGGBB, but its color is 1A212ED1",
        ),
        (
            format!(
                "  {camera}\n  di\n    gfx_draw_cube_wires 0d0_f32 1d0_f32 0d0_f32 2d0_f32 2d0_f32 2d0_f32 color_shade\n  do"
            ),
            "test:5:5: gfx_draw_cube_wires needs an opaque color, as RRGGBB, but its color is 1A212ED1",
        ),
        (
            format!(
                "  {camera}\n  di\n    gfx_draw_grid 20_u32 1d0_f32 color_solid color_shade\n  do"
            ),
            "test:5:5: gfx_draw_grid needs an opaque color, as RRGGBB, but its line color is 1A212ED1",
        ),
    ];
    for (frame, expected) in cases {
        let failing = load_with(ALPHA_REGINA, &frame_program(&frame))
            .unwrap_or_else(|message| panic!("{message}"));
        let path = failing.path().to_owned();
        let result = bosonoga::gfx::run(&failing, &mut Scenes(Vec::new()));
        assert_eq!(
            result.map_err(|message| message.replacen(&path, "test", 1)),
            Err(expected.to_owned())
        );
    }
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

#[cfg(test)]
const DICE: &str = "aka_i32 dice is from 0_i32 to 6_i32 except 3_i32 4_i32\n";

// A program of DICE, of score_fn, whose only line is given, and of main.
#[cfg(test)]
fn dice_program(line: &str, main: &str) -> String {
    format!(
        "{DICE}def score_fn roll_dice ret 0_i32\ndi\n  {line}\n  return roll\ndo\nbosonoga_main\ndi\n  {main}\n  return exit_success\ndo\n"
    )
}

#[cfg(test)]
const TO_I32: &str = "val roll is to_i32 roll_dice";

#[cfg(test)]
fn dice_call(argument: &str) -> String {
    format!("val score is score_fn of {argument} with ret 0_i32\n  info score")
}

#[test]
fn aka_values() {
    let far = "aka_i32 far except -1_i32 0_i32 1_i32\ndef f_fn x_far ret 0_i32\ndi\n  val x is to_i32 x_far\n  return x\ndo\n";
    let refused = |given: i32| {
        format!(
            "'score_fn' needs dice, an i32 from 0_i32 to 6_i32 except 3_i32 4_i32, but is given {given}_i32"
        )
    };
    let refused_far = |given: i32| {
        format!("'f_fn' needs far, an i32 except -1_i32 0_i32 1_i32, but is given {given}_i32")
    };
    for given in -8..=8 {
        let dice = [0, 1, 2, 5].contains(&given);
        let is_far = !(-1..=1).contains(&given);
        let far_call = |argument: &str| {
            format!(
                "{far}bosonoga_main\ndi\n  var got is {given}_i32\n  val x is f_fn of {argument} with ret 0_i32\n  info x got\n  return exit_success\ndo\n"
            )
        };
        let literal = format!("{given}_i32");
        // Known before the program runs.
        for (source, holds, message) in [
            (
                dice_program(TO_I32, &dice_call(&literal)),
                dice,
                refused(given),
            ),
            (
                dice_program(TO_I32, &format!("val x is {literal}\n  {}", dice_call("x"))),
                dice,
                refused(given),
            ),
            (
                format!(
                    "const x is {literal}\n{}",
                    dice_program(TO_I32, &dice_call("x"))
                ),
                dice,
                refused(given),
            ),
            (far_call(&literal), is_far, refused_far(given)),
        ] {
            match bosonoga::parse(&source) {
                Ok(_) => assert!(holds, "{source}"),
                Err(diagnostic) => {
                    assert!(!holds, "{}", diagnostic.located("test", &source));
                    assert_eq!(diagnostic.message, message);
                }
            }
        }
        // Not known, so checked by the call.
        for (source, holds, message) in [
            (
                dice_program(TO_I32, &format!("var x is {literal}\n  {}", dice_call("x"))),
                dice,
                refused(given),
            ),
            (far_call("got"), is_far, refused_far(given)),
        ] {
            let program = match bosonoga::parse(&source) {
                Ok(program) => program,
                Err(diagnostic) => panic!("{}", diagnostic.located("test", &source)),
            };
            match bosonoga::run(&program) {
                Ok((_, status)) => {
                    assert!(holds, "{source}");
                    assert_eq!(status, ExitStatus::Success);
                }
                Err((output, diagnostic)) => {
                    assert!(!holds, "{source}");
                    assert_eq!(output, "");
                    assert_eq!(diagnostic.message, message);
                }
            }
        }
    }
}

// A call statement and a set check too, and point at the argument.
#[test]
fn aka_call_forms() {
    let void = "aka_i32 pip is from 1_i32 to 7_i32\ndef show_fn times_i32 count_pip ret_void\ndi\n  val count is to_i32 count_pip\n  info count times_i32\ndo\ndef two_fn count_pip ret 0_i32 1_i32\ndi\n  val count is to_i32 count_pip\n  return count count\ndo\ndef one_fn count_pip ret 0_i32\ndi\n  val count is to_i32 count_pip\n  return count\ndo\n";
    let cases = [
        ("call show_fn of 9_i32 x", "test:20:25: 'show_fn'"),
        (
            "call two_fn of x with ret 0_i32 as a di\n info a\n do",
            "test:20:18: 'two_fn'",
        ),
        (
            "set x to one_fn of x with ret 0_i32",
            "test:20:22: 'one_fn'",
        ),
    ];
    for (line, expected) in cases {
        // The other functions are called with a value that they take.
        let source = format!(
            "{void}bosonoga_main\ndi\n  var x is 0_i32\n  {line}\n  call show_fn of 1_i32 1_i32\n  call two_fn of 1_i32 with ret 0_i32 as b di\n info b\n do\n  val c is one_fn of 1_i32 with ret 0_i32\n  info c x\n  return exit_success\ndo\n"
        );
        let program = match bosonoga::parse(&source) {
            Ok(program) => program,
            Err(diagnostic) => panic!("{}", diagnostic.located("test", &source)),
        };
        match bosonoga::run(&program) {
            Ok(_) => panic!("{line} is not refused"),
            Err((_, diagnostic)) => assert_eq!(
                diagnostic.located("test", &source),
                format!("{expected} needs pip, an i32 from 1_i32 to 7_i32, but is given 0_i32")
            ),
        }
    }
}

#[test]
fn aka_declaration_diagnostics() {
    let cases = [
        (
            "pub aka_i32 d is from 0_i32 to 6_i32",
            "test:1:1: pub is only in a module",
        ),
        (
            "aka_i32 d is from 6_i32 to 6_i32",
            "test:1:19: from 6_i32 to 6_i32 has no values, as from is inclusive and to is exclusive",
        ),
        (
            "aka_i32 d is from 0_i32 to 6_i32 except 9_i32",
            "test:1:41: 9_i32 is not in from 0_i32 to 6_i32, so it needs no except",
        ),
        (
            "aka_i32 d is from 0_i32 to 6_i32 except 6_i32",
            "test:1:41: 6_i32 is not in from 0_i32 to 6_i32",
        ),
        (
            "aka_i32 d is from 0_i32 to 6_i32 except 4_i32 3_i32",
            "test:1:47: the values of except are once each and increase, so 3_i32 cannot follow 4_i32",
        ),
        (
            "aka_i32 d except 3_i32 3_i32",
            "test:1:24: the values of except are once each and increase",
        ),
        (
            "aka_i32 d is from 0_i32 to 6_i32 except 0_i32 3_i32",
            "test:1:41: except is for values inside the range, and this one is at its edge, so write from 1_i32 to 6_i32",
        ),
        (
            "aka_i32 d is from 0_i32 to 6_i32 except 2_i32 4_i32 5_i32",
            "test:1:53: except is for values inside the range, and this one is at its edge, so write from 0_i32 to 4_i32",
        ),
        (
            "aka_i32 d is from 0_i32 to 2_i32 except 0_i32 1_i32",
            "test:1:41: d has no values, as except has them all",
        ),
        (
            "aka_i32 d is from 0_i64 to 6_i32",
            "test:1:19: aka_i32 needs i32, but '0_i64' is i64",
        ),
        ("aka_i32 d except nihil_i32", "test:1:18: aka_i32 needs i32"),
        (
            "bosonoga_global\ndi\n  val x is 1_i32\ndo\naka_i32 d except x",
            "test:5:18: the values of an aka_i32 are literals or earlier constants, so 'x' cannot be one",
        ),
        (
            "aka_i32 two_dice except 1_i32",
            "test:1:9: the name of an aka_i32 is one word",
        ),
        (
            "aka_i32 fn except 1_i32",
            "test:1:9: the name of an aka_i32 is one word",
        ),
        (
            "aka_i32 bool except 1_i32",
            "test:1:9: 'bool' is already a type",
        ),
        (
            "aka_i32 dir except 1_i32",
            "test:1:9: 'dir' is already a type",
        ),
        (
            "aka_i32 d except 1_i32\naka_i32 d except 2_i32",
            "test:2:9: aka_i32 'd' already exists",
        ),
        (
            "const d is 1_i32\naka_i32 d except 1_i32",
            "test:2:9: a constant with this name already exists",
        ),
        (
            "aka_i32 d except 1_i32\nconst d is 1_i32",
            "test:2:7: 'd' is already an aka_i32",
        ),
        (
            "aka_i32 d except 1_i32\nbosonoga_main\ndi\n  return exit_success\ndo",
            "test:1:9: 'd' is never the type of a parameter",
        ),
    ];
    let mut sources = Vec::new();
    for (source, expected) in cases {
        sources.push((format!("{source}\n"), expected));
    }
    assert_diagnostics(&sources);
}

#[test]
fn aka_syntax_diagnostics() {
    let cases = [
        (
            "aka_i32 d",
            "test:2:1: expected is or except, got end of file",
        ),
        (
            "aka_i32 d six",
            "test:1:11: expected is or except, got 'six'",
        ),
        (
            "aka_i32 d is except 3_i32",
            "test:1:14: expected from, got 'except'",
        ),
        (
            "aka_i32 d is 3_i32",
            "test:1:14: expected from, got '3_i32'",
        ),
        (
            "aka_i32 d is from 0_i32 to_add 6_i32",
            "test:1:25: expected to, got 'to_add'",
        ),
        (
            "aka_i32 d is from 0_i32 to 6_i32 3_i32",
            "test:1:34: expected except, aka_i32, const, def",
        ),
        (
            "aka_i32 d except",
            "test:2:1: expected a value, got end of file",
        ),
        ("aka_i64 d except 3_i64", "test:1:1: expected "),
        ("aka d except 3_i32", "test:1:1: expected "),
        (
            "const from is 1_i32",
            "test:1:7: expected a variable name, got 'from'",
        ),
        (
            "const except is 1_i32",
            "test:1:7: expected a variable name, got 'except'",
        ),
        (
            "const aka_x is 1_i32",
            "test:1:7: expected a variable name, got 'aka_x'",
        ),
        (
            "bosonoga_main\ndi\n  aka_i32 d except 1_i32\ndo",
            "test:3:3: expected a statement, got 'aka_i32'",
        ),
    ];
    let mut sources = Vec::new();
    for (source, expected) in cases {
        sources.push((format!("{source}\n"), expected));
    }
    assert_diagnostics(&sources);
}

#[test]
fn aka_use_diagnostics() {
    let cast = "test:4:LINE 'roll_dice' is dice, an aka_i32, which is converted before its other uses, as in val roll is to_i32 roll_dice";
    let uses = [
        ("val roll is roll_dice", 15),
        ("var roll is 1_i32\n  set roll to roll_dice", 15),
        ("var roll is 1_i32\n  set roll to_add roll_dice", 19),
        ("val roll is to_i64 roll_dice", 22),
        ("val roll is to_optional_i32 roll_dice", 31),
        ("val roll is math_to_abs roll_dice", 27),
        ("val roll is math_to_min 1_i32 roll_dice", 33),
        ("info roll_dice", 8),
        ("if roll_dice eq 5_i32 di\n nihil\n do", 6),
        ("if 5_i32 eq roll_dice di\n nihil\n do", 15),
        ("if some roll_dice di\n nihil\n do", 11),
        ("return roll_dice", 10),
        ("var xs is array 0_i32 with_len_col roll_dice", 38),
    ];
    for (line, column) in uses {
        let source = dice_program(line, &dice_call("1_i32"));
        let row = if line.contains("\n  set") { 5 } else { 4 };
        let expected = cast.replace("4:LINE", &format!("{row}:{column}:"));
        assert_eq!(diagnostic(&source), expected, "{line}");
    }
    let other = "aka_i32 pip is from 0_i32 to 6_i32\ndef pip_fn x_pip ret_void\ndi\n  val x is to_i32 x_pip\n  info x\ndo\n";
    let cases = [
        // Not the same aka_i32, even of more values.
        (
            format!(
                "{other}{}",
                dice_program("call pip_fn of roll_dice", &dice_call("1_i32"))
            ),
            "test:10:18: 'roll_dice' is dice, an aka_i32, which is converted",
        ),
        (
            dice_program("set roll_dice to 1_i32", &dice_call("1_i32")),
            "test:4:7: 'roll_dice' is a parameter, which is a val and cannot be set, but a copy can, as in var roll is to_i32 roll_dice",
        ),
        (
            dice_program(TO_I32, &dice_call("dice")),
            "test:9:28: 'dice' is an aka_i32, which is only the type of a parameter, as in value_dice",
        ),
        (
            dice_program(TO_I32, &dice_call("1_i64")),
            "test:9:28: score_fn needs i32, but '1_i64' is i64",
        ),
        (
            dice_program(TO_I32, &dice_call("1_i32 2_i32")),
            "test:9:34: 'score_fn' takes 1 value, dice, but is given more",
        ),
        (
            dice_program(TO_I32, "val dice is 1_i32"),
            "test:9:7: 'dice' is already an aka_i32",
        ),
        (
            dice_program(TO_I32, &dice_call("1_dice")),
            "test:9:28: number must have the suffix",
        ),
        (
            format!("{DICE}def f_fn x_optional_dice ret_void\ndi\n  nihil\ndo\n"),
            "test:2:10: 'dice' is an aka_i32, which cannot be optional",
        ),
        (
            format!("{DICE}def f_fn x_dice ret 0_dice\ndi\n  return x_dice\ndo\n"),
            "test:2:21: number must have the suffix",
        ),
        // Declared before the def.
        (
            format!("def f_fn x_dice ret_void\ndi\n  nihil\ndo\n{DICE}"),
            "test:1:10: expected a parameter whose name ends with its type",
        ),
        (
            dice_program("val roll is 1_i32", &dice_call("1_i32")),
            "test:2:14: 'roll_dice' is never read",
        ),
    ];
    assert_diagnostics(&cases);
}

// A module shows its pub aka_i32 to the file that reads it, and no other.
#[test]
fn aka_modules() {
    let directory = std::env::temp_dir().join(format!("bosonoga_aka_{}", std::process::id()));
    std::fs::create_dir_all(&directory).unwrap_or_else(|error| panic!("{error}"));
    let private = "aka_i32 dice except 3_i32\npub def f_fn x_dice ret 0_i32\ndi\n  val x is to_i32 x_dice\n  return x\ndo\n";
    let main = |def: &str, given: &str| {
        format!(
            "mod \"m.bosonoga\"_str\n{def}bosonoga_main\ndi\n  val x is f_fn of {given} with ret 0_i32\n  info x\n  return exit_success\ndo\n"
        )
    };
    let def = "def g_fn y_dice ret_void\ndi\n  val y is to_i32 y_dice\n  info y\ndo\n";
    let cases = [
        // A pub def has a private aka_i32, whose values it still needs.
        (private.to_owned(), main("", "2_i32"), None),
        (
            private.to_owned(),
            main("", "3_i32"),
            Some(":4:20: 'f_fn' needs dice, an i32 except 3_i32, but is given 3_i32".to_owned()),
        ),
        (
            private.to_owned(),
            main(def, "2_i32"),
            Some(format!(
                ":2:12: 'dice' is private to {}",
                directory.join("m.bosonoga").display()
            )),
        ),
        (
            format!("pub {private}"),
            main(&format!("{def}aka_i32 dice except 1_i32\n"), "2_i32"),
            Some(":7:9: aka_i32 'dice' already exists".to_owned()),
        ),
    ];
    for (module, program, expected) in cases {
        let path = directory.join("main.bosonoga");
        std::fs::write(directory.join("m.bosonoga"), module)
            .unwrap_or_else(|error| panic!("{error}"));
        std::fs::write(&path, program).unwrap_or_else(|error| panic!("{error}"));
        let got = bosonoga::load(&path, &Settings::default()).err();
        let wanted = expected.map(|message| format!("{}{message}", path.display()));
        assert_eq!(got, wanted);
    }
    std::fs::remove_dir_all(&directory).unwrap_or_else(|error| panic!("{error}"));
}
