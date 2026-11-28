mod aka;
mod array;
mod call;
mod draw;
mod item;
mod name;
mod statement;
mod then;
mod value;

use crate::ast::{self, Section, Span};
use crate::lexer::{Lexer, Tok, UNE_PREFIX};
use crate::program::{Program, Slot, Statement};
use crate::setting::Settings;
use crate::source::Sources;
use crate::value::{Kind, Value};
use crate::{Diagnostic, shown};

const SHIFT_BIT_MAX: u64 = 31;
const FUNCTION_VALUES_MAX: usize = 16;
const ARRAY_VALUES_MAX: usize = 65_536;

#[derive(Clone, Copy)]
struct Declaration {
    span: Span,
    scope: usize,
    public: bool,
    used: bool,
}

// The columns of an array, each named with its type, as in count_i32.
#[derive(Clone)]
struct Columns<'input> {
    names: Vec<&'input str>,
    kinds: Vec<Kind>,
}

impl Columns<'_> {
    fn position(&self, name: &str) -> Option<usize> {
        self.names.iter().position(|column| *column == name)
    }

    // As they are written, as in column_i32 rank_i32 loaded_bool.
    fn shown(&self) -> String {
        self.names.join(" ")
    }

    fn first(&self) -> &str {
        self.names.first().copied().unwrap_or("count_i32")
    }
}

struct Variable<'input> {
    name: &'input str,
    kind: Kind,
    slot: Slot,
    mutable: bool,
    array: Option<(usize, Columns<'input>)>,
    declaration: Declaration,
    origin: Origin,
    written: bool,
    // The aka_i32 of a parameter, which is only converted with to_i32 or
    // given to a parameter of the same aka_i32.
    aka: Option<usize>,
    // The value of an i32 val that is a literal, which is known before the
    // program runs.
    known: Option<i32>,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Origin {
    Declared,
    // A parameter of a def, which is a val.
    Parameter,
    // A variable of global that a def lists in with from_global,
    // which is used once it is read or set.
    Captured,
}

// An aka_i32, whose values are at the same index in the program.
struct Aka<'input> {
    name: &'input str,
    declaration: Declaration,
}

#[derive(Clone, Copy)]
struct Parameter {
    kind: Kind,
    aka: Option<usize>,
}

impl Parameter {
    fn plain(kind: Kind) -> Self {
        Parameter { kind, aka: None }
    }
}

struct Constant<'input> {
    name: &'input str,
    value: Value,
    declaration: Declaration,
}

#[derive(Clone, Copy)]
enum Callable {
    Function(usize),
    Array(usize),
    VarArray(usize),
}

impl Callable {
    fn is_function(self) -> bool {
        matches!(self, Callable::Function(_))
    }
}

// Where a def that draws is called: in gfx_frame, outside the block of
// gfx_camera_3d when it draws in 2D or has a camera of its own, and inside
// it when it draws in 3D.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Space {
    Flat,
    Camera,
}

struct Function<'input> {
    name: &'input str,
    callable: Callable,
    parameters: Vec<Parameter>,
    results: Vec<Kind>,
    // The names of the columns of an array, or of the values of a def.
    columns: Vec<&'input str>,
    declaration: Declaration,
    // Whether the def draws, and where, so that only gfx_frame calls it.
    draws: Option<Space>,
    // Whether the def reads gfx_key_held, so that only gfx_frame,
    // gfx_key_down and gfx_key_up call it.
    input: bool,
}

impl Function<'_> {
    fn noun(&self) -> &'static str {
        if self.callable.is_function() {
            "function"
        } else {
            "array"
        }
    }

    fn gives(&self) -> &'static str {
        if self.callable.is_function() {
            "returns"
        } else {
            "has the columns"
        }
    }
}

struct Scope {
    file: usize,
    parent: usize,
}

struct Checker<'a, 'input> {
    sources: &'a Sources,
    settings: &'a Settings,
    files: &'a [ast::File<'input>],
    modules: &'a [Vec<usize>],
    scopes: Vec<Scope>,
    scope: usize,
    variables: Vec<Variable<'input>>,
    akas: Vec<Aka<'input>>,
    constants: Vec<Constant<'input>>,
    functions: Vec<Function<'input>>,
    slot_count: usize,
    section: Option<Section>,
    function: Option<usize>,
    depth: usize,
    // Inside the block of a gfx_camera_3d, where only 3D is drawn.
    camera: bool,
    seen: Vec<Section>,
    program: Program,
}

pub(crate) fn check(
    sources: &Sources,
    settings: &Settings,
    files: &[ast::File<'_>],
    modules: &[Vec<usize>],
) -> Result<Program, Diagnostic> {
    let mut checker = Checker {
        sources,
        settings,
        files,
        modules,
        scopes: vec![Scope { file: 0, parent: 0 }],
        scope: 0,
        variables: Vec::new(),
        akas: Vec::new(),
        constants: Vec::new(),
        functions: Vec::new(),
        slot_count: 0,
        section: None,
        function: None,
        depth: 0,
        camera: false,
        seen: Vec::new(),
        program: Program {
            background: settings.background,
            mouse: settings.mouse,
            ..Program::default()
        },
    };
    checker.items(0)?;
    if checker.program.sections.main.is_none() {
        let mut end = 0;
        if let Some(file) = files.first() {
            end = file.end;
        }
        let message = if checker.program.is_gfx {
            "a gfx program needs a gfx_main block"
        } else {
            "a program needs a main block"
        };
        return Err(Diagnostic::new(end, message));
    }
    checker.check_all_used()?;
    checker.program.slot_count = checker.slot_count;
    Ok(checker.program)
}

fn is_function_name(name: &str) -> bool {
    name.len() > "_fn".len() && name.ends_with("_fn")
}

// Whether a statement always returns, so that nothing after it runs: a
// return, an if whose branches and else all return, or a call or a builtin
// whose block returns, as that block always runs. A while may not run.
pub(super) fn returns(statement: &ast::Statement<'_>) -> bool {
    match statement {
        ast::Statement::Return(..) | ast::Statement::ReturnValues(..) => true,
        ast::Statement::If {
            branches,
            otherwise,
        } => {
            let mut all = otherwise.as_ref().is_some_and(|block| block_returns(block));
            for (_, block) in branches {
                all = all && block_returns(block);
            }
            all
        }
        ast::Statement::Call {
            taking: Some(taking),
            ..
        } => block_returns(&taking.block),
        ast::Statement::Given { block, .. } => block_returns(block),
        ast::Statement::Info(_)
        | ast::Statement::Call { taking: None, .. }
        | ast::Statement::Join(..)
        | ast::Statement::Set { .. }
        | ast::Statement::SetConversion { .. }
        | ast::Statement::SetCall { .. }
        | ast::Statement::Declare { .. }
        | ast::Statement::DeclareArray { .. }
        | ast::Statement::SetAt { .. }
        | ast::Statement::While(..)
        | ast::Statement::For { .. }
        | ast::Statement::AwaitNextFrame(_)
        | ast::Statement::Draw { .. }
        | ast::Statement::Camera3d { .. }
        | ast::Statement::MouseCapture(..)
        | ast::Statement::Pass => false,
    }
}

pub(super) fn block_returns(block: &[ast::Located<'_>]) -> bool {
    for located in block {
        if returns(&located.statement) {
            return true;
        }
    }
    false
}

fn values_shown(kinds: &[Kind]) -> String {
    let mut names = Vec::new();
    for kind in kinds {
        names.push(kind.name());
    }
    names_shown(&names)
}

fn names_shown(names: &[&str]) -> String {
    if names.is_empty() {
        return "no values".to_owned();
    }
    let noun = if names.len() == 1 { "value" } else { "values" };
    format!("{} {noun}, {}", names.len(), names.join(" "))
}

impl<'a, 'input> Checker<'a, 'input> {
    fn text(&self, span: Span) -> &'a str {
        self.sources.text(span)
    }

    fn fail<T>(offset: usize, message: impl Into<String>) -> Result<T, Diagnostic> {
        Err(Diagnostic::new(offset, message))
    }

    fn next_token(&self, after: usize) -> (usize, String) {
        let Some(file) = self.sources.file_at(after) else {
            return (after, "end of file".to_owned());
        };
        let mut lexer = Lexer::starting_at(&file.text, file.base, after);
        match lexer.next() {
            Some(Ok((start, token, end))) => {
                if token == Tok::End {
                    return (start, "end of file".to_owned());
                }
                (start, format!("'{}'", shown(file.slice(start, end))))
            }
            Some(Err(diagnostic)) => (diagnostic.offset, "end of file".to_owned()),
            None => (after, "end of file".to_owned()),
        }
    }

    fn expected<T>(&self, after: usize, what: &str) -> Result<T, Diagnostic> {
        let (at, got) = self.next_token(after);
        Self::fail(at, format!("expected {what}, got {got}"))
    }

    fn expected_at<T>(&self, span: Span, what: &str) -> Result<T, Diagnostic> {
        Self::fail(
            span.start,
            format!("expected {what}, got '{}'", self.text(span)),
        )
    }

    fn declaration(&self, span: Span, public: bool) -> Declaration {
        Declaration {
            span,
            scope: self.scope,
            public,
            used: self.text(span).starts_with(UNE_PREFIX),
        }
    }

    fn parent_of(&self, scope: usize) -> usize {
        match self.scopes.get(scope) {
            Some(found) => found.parent,
            None => 0,
        }
    }

    fn visible(&self, declaration: &Declaration) -> bool {
        let mut home = declaration.scope;
        if declaration.public {
            home = self.parent_of(declaration.scope);
        }
        let mut scope = self.scope;
        while scope != home {
            if scope == 0 {
                return false;
            }
            scope = self.parent_of(scope);
        }
        true
    }

    fn constant_any(&self, name: &str) -> Option<usize> {
        for (index, constant) in self.constants.iter().enumerate() {
            if constant.name == name {
                return Some(index);
            }
        }
        None
    }

    fn constant(&self, name: &str) -> Option<usize> {
        let index = self.constant_any(name)?;
        if self.visible(&self.constants[index].declaration) {
            Some(index)
        } else {
            None
        }
    }

    fn variable_any(&self, name: &str) -> Option<usize> {
        for (index, variable) in self.variables.iter().enumerate() {
            if variable.name == name {
                return Some(index);
            }
        }
        None
    }

    fn variable(&self, name: &str) -> Option<usize> {
        let index = self.variable_any(name)?;
        if self.visible(&self.variables[index].declaration) {
            Some(index)
        } else {
            None
        }
    }

    fn function_any(&self, name: &str) -> Option<usize> {
        for (index, function) in self.functions.iter().enumerate() {
            if function.name == name {
                return Some(index);
            }
        }
        None
    }

    fn function(&self, name: &str) -> Option<usize> {
        let index = self.function_any(name)?;
        if self.visible(&self.functions[index].declaration) {
            Some(index)
        } else {
            None
        }
    }

    // Whether a declaration here is a variable of global.
    fn in_global(&self) -> bool {
        self.section == Some(Section::Global) && self.function.is_none()
    }

    // Whether the mouse can be captured, released or asked about here: in
    // gfx_frame, gfx_key_down or gfx_key_up, outside defs.
    fn in_mouse_section(&self) -> bool {
        matches!(
            self.section,
            Some(Section::GfxFrame | Section::GfxKeyDown | Section::GfxKeyUp)
        ) && self.function.is_none()
    }

    fn allocate(&mut self) -> Slot {
        self.slot_count += 1;
        Slot::new(self.slot_count - 1, self.in_global())
    }

    fn declare(&mut self, (span, name): ast::Named<'input>, kind: Kind, slot: Slot, mutable: bool) {
        let declaration = self.declaration(span, false);
        self.variables.push(Variable {
            name,
            kind,
            slot,
            mutable,
            array: None,
            declaration,
            origin: Origin::Declared,
            written: false,
            aka: None,
            known: None,
        });
    }

    fn public(&self, public: Option<Span>) -> Result<bool, Diagnostic> {
        match public {
            Some(span) if self.scope == 0 => {
                Self::fail(span.start, "pub is only in a module, a file read with mod")
            }
            Some(_) => Ok(true),
            None => Ok(false),
        }
    }

    fn in_main(&self) -> bool {
        matches!(self.section, Some(Section::Main | Section::GfxMain))
    }

    fn check_variables_used(&self, first: usize) -> Result<(), Diagnostic> {
        for variable in self.variables.iter().skip(first) {
            let span = variable.declaration.span;
            if variable.origin == Origin::Captured {
                if !variable.declaration.used && !variable.written {
                    return Self::fail(
                        span.start,
                        format!(
                            "'{}' is in with from_global, but the function never uses it",
                            self.text(span)
                        ),
                    );
                }
            } else if !variable.declaration.used {
                return Self::fail(span.start, format!("'{}' is never read", self.text(span)));
            }
        }
        Ok(())
    }

    fn check_all_used(&self) -> Result<(), Diagnostic> {
        for aka in &self.akas {
            if !aka.declaration.used {
                let span = aka.declaration.span;
                return Self::fail(
                    span.start,
                    format!("'{}' is never the type of a parameter", self.text(span)),
                );
            }
        }
        for constant in &self.constants {
            if !constant.declaration.used {
                let span = constant.declaration.span;
                return Self::fail(span.start, format!("'{}' is never used", self.text(span)));
            }
        }
        for function in &self.functions {
            if !function.declaration.used {
                let span = function.declaration.span;
                let what = if function.callable.is_function() {
                    "called"
                } else {
                    "read"
                };
                return Self::fail(span.start, format!("'{}' is never {what}", self.text(span)));
            }
        }
        self.check_variables_used(0)
    }

    fn block(&mut self, block: &[ast::Located<'input>]) -> Result<Vec<Statement>, Diagnostic> {
        self.depth += 1;
        let visible = self.variables.len();
        let mut statements = Vec::new();
        let mut returned = false;
        for located in block {
            if returned {
                self.depth -= 1;
                return Self::fail(
                    located.start,
                    "this is never reached, as the statement before it always returns",
                );
            }
            returned = returns(&located.statement);
            match self.statement(&located.statement) {
                Ok(checked) => statements.extend(checked),
                Err(diagnostic) => {
                    self.depth -= 1;
                    return Err(diagnostic);
                }
            }
        }
        self.depth -= 1;
        let shared =
            self.section == Some(Section::Global) && self.depth == 0 && self.function.is_none();
        if !shared {
            self.check_variables_used(visible)?;
            self.variables.truncate(visible);
        }
        Ok(statements)
    }
}
