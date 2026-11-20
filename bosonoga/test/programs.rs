use std::path::Path;

use bosonoga::ast::ExitStatus;
use bosonoga::settings::Settings;

#[cfg(test)]
fn check_program(path: &str, source: &str) {
    let program = bosonoga::parse(source)
        .unwrap_or_else(|diagnostic| panic!("{}", diagnostic.located(path, source)));
    let (_, status) = bosonoga::run(&program)
        .unwrap_or_else(|(_, diagnostic)| panic!("{}", diagnostic.located(path, source)));
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
fn conversions() {
    check_program(
        "test/conversion.bosonoga",
        include_str!("conversion.bosonoga"),
    );
}

#[test]
fn modules() {
    let program = bosonoga::load(Path::new("test/module.bosonoga"), &Settings::default())
        .unwrap_or_else(|message| panic!("{message}"));
    let (_, status) = bosonoga::run(&program)
        .unwrap_or_else(|(_, diagnostic)| panic!("{}", program.located(&diagnostic)));
    assert_eq!(status, ExitStatus::Success);
}
