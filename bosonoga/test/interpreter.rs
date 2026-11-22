use std::path::Path;

use bosonoga::ast::ExitStatus;
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
