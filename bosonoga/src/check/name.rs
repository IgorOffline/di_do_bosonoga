use super::aka::copy_name;
use super::{Checker, Declaration, is_function_name};
use crate::Diagnostic;
use crate::ast::{Named, Span};
use crate::program::Slot;
use crate::setting::Asset;
use crate::value::{Kind, Value};

fn asset_index(assets: &mut Vec<(String, Asset)>, key: &str, asset: &Asset) -> usize {
    for (index, (known, _)) in assets.iter().enumerate() {
        if known == key {
            return index;
        }
    }
    assets.push((key.to_owned(), asset.clone()));
    assets.len() - 1
}

impl<'input> Checker<'_, 'input> {
    pub(super) fn is_reserved(&self, name: &str) -> bool {
        let settings = self.settings;
        if let Some(key) = name.strip_prefix("color_")
            && settings.color(key).is_some()
        {
            return true;
        }
        if let Some(key) = name.strip_prefix("key_")
            && settings.key(key).is_some()
        {
            return true;
        }
        if let Some(key) = name.strip_prefix("font_")
            && settings.font(key).is_some()
        {
            return true;
        }
        if let Some(key) = name.strip_prefix("icon_")
            && settings.icon(key).is_some()
        {
            return true;
        }
        if let Some(key) = name.strip_prefix("dir_")
            && settings.dir(key).is_some()
        {
            return true;
        }
        if let Some(key) = name.strip_prefix("setting_")
            && settings.value(key).is_some()
        {
            return true;
        }
        false
    }

    pub(super) fn setting(&mut self, name: &str) -> Option<Value> {
        let settings = self.settings;
        if let Some(key) = name.strip_prefix("color_") {
            let color = settings.color(key)?;
            return Some(Value::Color(color));
        }
        if let Some(key) = name.strip_prefix("key_") {
            let code = settings.key(key)?;
            return Some(Value::InputKey(code));
        }
        if let Some(key) = name.strip_prefix("font_") {
            let asset = settings.font(key)?;
            let index = asset_index(&mut self.program.fonts, key, asset);
            return Some(Value::Font {
                index,
                text: asset.text.clone(),
            });
        }
        if let Some(key) = name.strip_prefix("icon_") {
            let asset = settings.icon(key)?;
            let index = asset_index(&mut self.program.icons, key, asset);
            return Some(Value::Icon {
                index,
                text: asset.text.clone(),
            });
        }
        let key = name.strip_prefix("setting_")?;
        let value = settings.value(key)?;
        Some(value.clone())
    }

    fn hidden(&self, name: &str) -> Option<&Declaration> {
        if let Some(index) = self.aka_any(name) {
            return Some(&self.akas[index].declaration);
        }
        if let Some(index) = self.constant_any(name) {
            return Some(&self.constants[index].declaration);
        }
        if let Some(index) = self.variable_any(name) {
            return Some(&self.variables[index].declaration);
        }
        if let Some(index) = self.function_any(name) {
            return Some(&self.functions[index].declaration);
        }
        None
    }

    fn scope_path(&self, scope: usize) -> &str {
        let Some(found) = self.scopes.get(scope) else {
            return "";
        };
        match self.sources.files.get(found.file) {
            Some(file) => &file.path,
            None => "",
        }
    }

    pub(super) fn check_not_hidden(&self, span: Span, name: &str) -> Result<(), Diagnostic> {
        let Some(declaration) = self.hidden(name) else {
            return Ok(());
        };
        if self.visible(declaration) {
            return Ok(());
        }
        let shown = self.text(span);
        let path = self.scope_path(declaration.scope);
        if !declaration.public {
            return Self::fail(span.start, format!("'{shown}' is private to {path}"));
        }
        let parent = self.parent_of(declaration.scope);
        Self::fail(
            span.start,
            format!(
                "'{shown}' is pub in {path}, so only {}, which reads it with mod, sees it",
                self.scope_path(parent)
            ),
        )
    }

    pub(super) fn fail_unknown_variable<T>(&self, span: Span, name: &str) -> Result<T, Diagnostic> {
        self.check_not_hidden(span, name)?;
        let shown = self.text(span);
        if self.aka_any(name).is_some() {
            return Self::fail(
                span.start,
                format!(
                    "'{shown}' is an aka_i32, which is only the type of a parameter, as in value_{shown}"
                ),
            );
        }
        if let Some(index) = self.variable(name)
            && self.variables[index].array.is_some()
        {
            return Self::fail(
                span.start,
                format!("'{shown}' is an array, which is read with var, set ... to or call"),
            );
        }
        if let Some(index) = self.function(name) {
            let (what, done) = if self.functions[index].callable.is_function() {
                ("a function", "called")
            } else {
                ("an array", "read")
            };
            return Self::fail(
                span.start,
                format!("'{shown}' is {what}, which is {done} with var, set ... to or call"),
            );
        }
        if is_function_name(name) {
            return Self::fail(span.start, format!("unknown function '{shown}'"));
        }
        Self::fail(span.start, format!("unknown variable '{shown}'"))
    }

    pub(super) fn new_name(&self, (span, name): Named<'input>) -> Result<(), Diagnostic> {
        if self.is_reserved(name) {
            return self.expected_at(span, "a variable name");
        }
        let shown = self.text(span);
        if self.aka_any(name).is_some() {
            return Self::fail(span.start, format!("'{shown}' is already an aka_i32"));
        }
        if self.constant_any(name).is_some() {
            return Self::fail(span.start, "a constant with this name already exists");
        }
        if self.variable_any(name).is_some() {
            return Self::fail(span.start, format!("variable '{shown}' already exists"));
        }
        if is_function_name(name) {
            return Self::fail(
                span.start,
                format!("'{shown}' ends with _fn, which only the name of a function does"),
            );
        }
        if let Some(index) = self.function_any(name) {
            let what = if self.functions[index].callable.is_function() {
                "a function"
            } else {
                "an array"
            };
            return Self::fail(span.start, format!("'{shown}' is already {what}"));
        }
        Ok(())
    }

    pub(super) fn target(
        &mut self,
        (span, name): Named<'input>,
    ) -> Result<(Slot, Kind), Diagnostic> {
        if self.is_reserved(name) {
            return self.expected_at(span, "a variable name");
        }
        let shown = self.text(span);
        if self.constant(name).is_some() {
            return Self::fail(
                span.start,
                format!("'{shown}' is a constant and cannot be set"),
            );
        }
        let Some(index) = self.variable(name) else {
            if let Some(function) = self.function(name)
                && let super::Callable::Array(_) = self.functions[function].callable
            {
                return Self::fail(
                    span.start,
                    format!(
                        "'{shown}' is a val array and cannot be set, but a var array of a block can"
                    ),
                );
            }
            return self.fail_unknown_variable(span, name);
        };
        let variable = &self.variables[index];
        if let Some((_, columns)) = &variable.array {
            return Self::fail(
                span.start,
                format!(
                    "'{shown}' is an array, whose rows are set with set {shown} at INDEX to and one value per column, and one column with set {shown} at INDEX {} to",
                    columns.first()
                ),
            );
        }
        if let Some(aka) = variable.aka {
            let copy = self.aka_base(span, aka);
            return Self::fail(
                span.start,
                format!(
                    "'{shown}' is a parameter, which is a val and cannot be set, but a copy can, as in var {copy} is to_i32 {shown}"
                ),
            );
        }
        if variable.origin == super::Origin::Parameter {
            let suffix = format!("_{}", variable.kind.name());
            let copy = copy_name(shown.strip_suffix(&suffix).unwrap_or("copy"));
            return Self::fail(
                span.start,
                format!(
                    "'{shown}' is a parameter, which is a val and cannot be set, but a copy can, as in var {copy} is {shown}"
                ),
            );
        }
        if !variable.mutable {
            return Self::fail(span.start, format!("'{shown}' is a val and cannot be set"));
        }
        let found = (variable.slot, variable.kind);
        self.variables[index].written = true;
        Ok(found)
    }
}
