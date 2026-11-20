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
