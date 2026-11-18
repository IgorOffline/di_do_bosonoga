use bosonoga::ast::ExitStatus;

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
