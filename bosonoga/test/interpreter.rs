use std::path::Path;

use bosonoga::ast::ExitStatus;
use bosonoga::gfx::{Input, Scene, Window};
use bosonoga::settings::Settings;

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
            keys: Vec::new(),
            delta_time: 1.0 / 60.0,
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
