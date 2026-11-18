use crate::Diagnostic;
use crate::ast::ExitStatus;
use crate::value::{Compare, Value};

#[derive(Debug)]
pub(crate) enum Operand {
    Literal(Value),
    Slot(usize),
}

#[derive(Debug)]
pub(crate) struct Comparison {
    pub(crate) negate: bool,
    pub(crate) left: Operand,
    pub(crate) compare: Compare,
    pub(crate) right: Operand,
}

pub(crate) type Condition = Vec<Vec<Comparison>>;

#[derive(Debug)]
pub(crate) enum Statement {
    Info(Vec<Operand>),
    Calculate {
        slot: usize,
        operation: crate::value::Operation,
        values: Vec<Operand>,
        offset: usize,
    },
    Assign {
        slot: usize,
        initial: Operand,
        shift_or: Vec<Operand>,
    },
    If {
        branches: Vec<(Condition, Vec<Statement>)>,
        otherwise: Option<Vec<Statement>>,
    },
    Return(ExitStatus),
}

#[derive(Debug)]
pub struct Program {
    pub(crate) statements: Vec<Statement>,
    pub(crate) slot_count: usize,
    pub(crate) end: usize,
}

struct Execution {
    slots: Vec<Value>,
    output: String,
}

impl Execution {
    fn get<'a>(&'a self, operand: &'a Operand) -> &'a Value {
        match operand {
            Operand::Literal(value) => value,
            Operand::Slot(slot) => &self.slots[*slot],
        }
    }

    fn holds(&self, condition: &Condition) -> bool {
        condition.iter().any(|conjunction| {
            conjunction.iter().all(|comparison| {
                self.get(&comparison.left)
                    .compare(comparison.compare, self.get(&comparison.right))
                    != comparison.negate
            })
        })
    }

    fn block(&mut self, statements: &[Statement]) -> Result<Option<ExitStatus>, Diagnostic> {
        for statement in statements {
            match statement {
                Statement::Calculate {
                    slot,
                    operation,
                    values,
                    offset,
                } => {
                    let operands: Vec<&Value> =
                        values.iter().map(|value| self.get(value)).collect();
                    let result = operation
                        .evaluate(&operands)
                        .map_err(|message| Diagnostic::new(*offset, message))?;
                    self.slots[*slot] = result;
                }
                Statement::Info(values) => {
                    for value in values {
                        let shown = self.get(value).to_string();
                        self.output.push_str(&shown);
                    }
                    self.output.push('\n');
                }
                Statement::Assign {
                    slot,
                    initial,
                    shift_or,
                } => {
                    let mut value = self.get(initial).clone();
                    for flags in shift_or {
                        if let (Value::Flags(bits), Value::Flags(more)) =
                            (&mut value, self.get(flags))
                        {
                            *bits |= more;
                        }
                    }
                    self.slots[*slot] = value;
                }
                Statement::If {
                    branches,
                    otherwise,
                } => {
                    let taken = branches
                        .iter()
                        .find(|(condition, _)| self.holds(condition))
                        .map(|(_, block)| block)
                        .or(otherwise.as_ref());
                    if let Some(block) = taken
                        && let Some(status) = self.block(block)?
                    {
                        return Ok(Some(status));
                    }
                }
                Statement::Return(status) => return Ok(Some(*status)),
            }
        }
        Ok(None)
    }
}

impl Program {
    pub fn run(&self) -> Result<(String, ExitStatus), (String, Diagnostic)> {
        let mut execution = Execution {
            slots: vec![Value::Bool(false); self.slot_count],
            output: String::new(),
        };
        match execution.block(&self.statements) {
            Ok(Some(status)) => Ok((execution.output, status)),
            Err(diagnostic) => Err((execution.output, diagnostic)),
            Ok(None) => Err((
                execution.output,
                Diagnostic::new(self.end, "the program ended without a return"),
            )),
        }
    }
}
