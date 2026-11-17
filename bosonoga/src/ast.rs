#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ExitStatus {
    Success,
    Failure,
}

#[derive(Debug, PartialEq, Eq)]
pub enum Statement {
    Info(Vec<i32>),
    Nihil,
    Return(ExitStatus),
}

#[derive(Debug)]
pub struct Program {
    pub statements: Vec<Statement>,
    /// The byte offset of the token after `bosonoga_main`'s block.
    pub end: usize,
}
