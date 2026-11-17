use bosonoga::ast::ExitStatus;

#[test]
fn info_prints_values_without_separators() {
    let program =
        bosonoga::parse("bosonoga_main di info -7_i32 0_i32 42_i32 return exit_failure do")
            .unwrap();
    assert_eq!(
        bosonoga::run(&program).unwrap(),
        ("-7042\n".to_owned(), ExitStatus::Failure)
    );
}
