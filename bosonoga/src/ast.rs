use crate::value::{Compare, Value};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ExitStatus {
    Success,
    Failure,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Span {
    pub start: usize,
    pub end: usize,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Term<'input> {
    Literal(Value),
    Shift(&'input str),
    Name(&'input str),
    Result(&'input str),
    GfxValue(&'input str),
}

#[derive(Clone, Debug, PartialEq)]
pub struct Operand<'input> {
    pub span: Span,
    pub term: Term<'input>,
}

#[derive(Debug, PartialEq)]
pub struct Comparison<'input> {
    pub negate: bool,
    pub left: Operand<'input>,
    pub right: Option<(Span, Compare, Operand<'input>)>,
}

pub type Condition<'input> = Vec<Vec<Comparison<'input>>>;

pub type Block<'input> = Vec<Statement<'input>>;

#[derive(Debug, PartialEq)]
pub enum Statement<'input> {
    Info(Vec<Operand<'input>>),
    Set {
        name: (Span, &'input str),
        operation: (Span, crate::value::Operation),
        values: Vec<Operand<'input>>,
    },
    Declare {
        name: (Span, &'input str),
        mutable: bool,
        initial: Operand<'input>,
        shift_or: Vec<(Span, Operand<'input>)>,
    },
    If {
        branches: Vec<(Condition<'input>, Block<'input>)>,
        otherwise: Option<Block<'input>>,
    },
    Nihil,
    Return(ExitStatus),
}

#[derive(Debug)]
pub struct Main<'input> {
    pub statements: Block<'input>,
    pub end: usize,
}
