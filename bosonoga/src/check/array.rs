use super::{ARRAY_VALUES_MAX, Checker, Columns, values_shown};
use crate::Diagnostic;
use crate::ast::{self, Named, Rows, Span, Term};
use crate::program::{self, Callee, Operand, Slot, Statement, VarArray};
use crate::value::{Kind, Operation, Value};

fn zero_value(kind: Kind) -> Option<Value> {
    match kind {
        Kind::I32 => Some(Value::I32(0)),
        Kind::I64 => Some(Value::I64(0)),
        Kind::U32 => Some(Value::U32(0)),
        Kind::U64 => Some(Value::U64(0)),
        Kind::F32 => Some(Value::F32(0.0)),
        Kind::F64 => Some(Value::F64(0.0)),
        Kind::Flags => Some(Value::Flags(0)),
        Kind::Color => Some(Value::Color(0)),
        Kind::Str => Some(Value::Str(String::new())),
        Kind::Bool => Some(Value::Bool(false)),
        Kind::InputKey | Kind::Font | Kind::Icon | Kind::Optional(_) => None,
    }
}

impl<'input> Checker<'_, 'input> {
    fn check_array_size(
        &self,
        span: Span,
        name: Span,
        rows: usize,
        kinds: &[Kind],
    ) -> Result<(), Diagnostic> {
        let values = rows.saturating_mul(kinds.len());
        if values <= ARRAY_VALUES_MAX {
            return Ok(());
        }
        Self::fail(
            span.start,
            format!(
                "'{}' would hold {values} values, and an array holds at most {ARRAY_VALUES_MAX}",
                self.text(name)
            ),
        )
    }

    pub(super) fn literal_rows(
        &mut self,
        span: Span,
        kinds: &[Kind],
        rows: &[(Span, Vec<ast::Operand<'input>>)],
        is_var: bool,
    ) -> Result<Vec<Vec<Value>>, Diagnostic> {
        let shown = self.text(span);
        let types = values_shown(kinds);
        let context = format!("a row of '{shown}'");
        let mut table = Vec::new();
        for (row_span, values) in rows {
            let mut row = Vec::new();
            for (index, kind) in kinds.iter().enumerate() {
                let Some(value) = values.get(index) else {
                    return Self::fail(
                        row_span.start,
                        format!("'{shown}' has rows of {types}, but this row has {index}"),
                    );
                };
                row.push(self.row_literal(value, *kind, &context)?);
            }
            if values.len() > kinds.len() {
                return Self::fail(
                    row_span.start,
                    format!("'{shown}' has rows of {types}, but this row has more"),
                );
            }
            table.push(row);
            if is_var {
                self.check_array_size(*row_span, span, table.len(), kinds)?;
            }
        }
        Ok(table)
    }

    fn row_literal(
        &mut self,
        value: &ast::Operand<'input>,
        kind: Kind,
        context: &str,
    ) -> Result<Value, Diagnostic> {
        let not_literal = Diagnostic::new(
            value.span.start,
            format!(
                "the values of a row are literals, like 7_i32, so a row cannot have '{}'",
                self.text(value.span)
            ),
        );
        if let Term::Name(word) = value.term
            && !self.is_reserved(word)
            && self.constant(word).is_none()
        {
            self.check_not_hidden(value.span, word)?;
            return Err(not_literal);
        }
        match self.value_of_kind(value, kind, context)? {
            Operand::Literal(literal) => Ok(literal),
            Operand::Slot(_) | Operand::KeyHeld(_) => Err(not_literal),
        }
    }

    pub(super) fn var_array(
        &mut self,
        (span, name): Named<'input>,
        mutable: bool,
        array: Span,
        column_list: &[ast::Column<'input>],
        rows: &Rows<'input>,
    ) -> Result<Statement, Diagnostic> {
        if !mutable {
            return Self::fail(
                array.start,
                "a val array is top-level, and an array of a block is a var",
            );
        }
        self.new_name((span, name))?;
        let columns = self.named_values(column_list, super::item::Values::Array)?;
        let kinds = columns.kinds.clone();
        let mut values = Vec::new();
        let repeat;
        match rows {
            Rows::Literal(literal) => {
                let table = self.literal_rows(span, &kinds, literal, true)?;
                for row in table {
                    values.extend(row);
                }
                repeat = 1;
            }
            Rows::Zeroed(keyword, count) => {
                repeat = self.zeroed_row_count(span, &kinds, count)?;
                for kind in &kinds {
                    let Some(zero) = zero_value(*kind) else {
                        return Self::fail(
                            keyword.start,
                            format!(
                                "a zeroed row cannot have {}, which has no zero, but literal rows can",
                                kind.name()
                            ),
                        );
                    };
                    values.push(zero);
                }
            }
        }
        let width = kinds.len();
        let row_count = match values.len().checked_div(width) {
            Some(per_repeat) => per_repeat * repeat,
            None => 0,
        };
        let slot = Slot::new(self.slot_count, self.in_global());
        self.slot_count += row_count * width;
        let index = self.program.var_arrays.len();
        self.program.var_arrays.push(VarArray {
            name: self.text(span).to_owned(),
            slot,
            columns: width,
            rows: row_count,
        });
        self.declare((span, name), Kind::I32, slot, true);
        if let Some(variable) = self.variables.last_mut() {
            variable.array = Some((index, columns));
        }
        Ok(Statement::ArrayInit {
            slot,
            values,
            repeat,
        })
    }

    fn zeroed_row_count(
        &mut self,
        name: Span,
        kinds: &[Kind],
        count: &ast::Operand<'input>,
    ) -> Result<usize, Diagnostic> {
        let mut row_count = 0;
        if let Operand::Literal(Value::I32(given)) =
            self.value_of_kind(count, Kind::I32, "with_len_col")?
            && let Ok(rows) = usize::try_from(given)
        {
            row_count = rows;
        }
        if row_count == 0 {
            return Self::fail(
                count.span.start,
                "with_len_col needs an i32 literal or constant above zero, as the size of an array is known before the program runs",
            );
        }
        self.check_array_size(count.span, name, row_count, kinds)?;
        Ok(row_count)
    }

    // set ... at, of a whole row, whose values are in the order of its
    // columns, or of one column, which is named, as in
    // set grid at 2_i32 count_i32 to_add 1_i32.
    pub(super) fn set_at(
        &mut self,
        (span, name): Named<'input>,
        index: &ast::Operand<'input>,
        column: Option<Named<'input>>,
        (operation_span, operation): (Span, Operation),
        values: &[ast::Operand<'input>],
    ) -> Result<Vec<Statement>, Diagnostic> {
        let mut found = None;
        if let Some(at) = self.variable(name) {
            let variable = &mut self.variables[at];
            if variable.array.is_some() {
                variable.written = true;
            }
            found.clone_from(&variable.array);
        }
        let Some((array, columns)) = found else {
            self.target((span, name))?;
            return self.expected(
                span.end,
                "to, to_add, to_subtract, to_multiply, to_divide, to_mod or a conversion",
            );
        };
        let shown = self.text(span);
        let index_operand =
            self.value_of_kind(index, Kind::I32, &format!("the index of '{shown}'"))?;
        if let Some(named) = column {
            let write = (array, index_operand, span.start);
            return self.set_column(
                shown,
                &columns,
                write,
                named,
                (operation_span, operation),
                values,
            );
        }
        if operation != Operation::Copy {
            return self.expected_at(operation_span, "to");
        }
        let kinds = &columns.kinds;
        let types = values_shown(kinds);
        let context = format!("a row of '{shown}'");
        let mut operands = Vec::new();
        for (position, kind) in kinds.iter().enumerate() {
            let Some(value) = values.get(position) else {
                return Self::fail(
                    span.start,
                    format!("'{shown}' has rows of {types}, but this row has {position}"),
                );
            };
            operands.push(self.value_of_kind(value, *kind, &context)?);
        }
        if let Some(extra) = values.get(kinds.len()) {
            return Self::fail(
                extra.span.start,
                format!("'{shown}' has rows of {types}, but this row has more"),
            );
        }
        Ok(vec![Statement::ArrayWrite {
            array,
            index: index_operand,
            first: 0,
            values: operands,
            offset: span.start,
        }])
    }

    // One column of a row: set ... to writes it, and an operation reads it
    // into a slot of its own, works on it there, and writes it back.
    fn set_column(
        &mut self,
        shown: &str,
        columns: &Columns<'input>,
        (array, index, offset): (usize, Operand, usize),
        (column_span, column): Named<'input>,
        operation: (Span, Operation),
        values: &[ast::Operand<'input>],
    ) -> Result<Vec<Statement>, Diagnostic> {
        let Some(position) = columns.position(column) else {
            return Self::fail(
                column_span.start,
                format!(
                    "'{shown}' has the columns {}, so it has no {column}",
                    columns.shown()
                ),
            );
        };
        let kind = columns.kinds[position];
        let slot = self.allocate();
        let mut statements = Vec::new();
        if operation.1 != Operation::Copy {
            statements.push(Statement::Call {
                slot,
                call: program::Call {
                    callee: Callee::VarArray(array),
                    arguments: vec![index.clone()],
                    aka_checks: Vec::new(),
                    offset,
                    column: position,
                },
            });
        }
        // The operation starts from the column, which a val of the same name,
        // taken from another row, can still be added to.
        let first = self.set_first((column_span, ""), (slot, kind), operation, values)?;
        statements.push(first);
        statements.push(Statement::ArrayWrite {
            array,
            index,
            first: position,
            values: vec![Operand::Slot(slot)],
            offset,
        });
        Ok(statements)
    }
}
