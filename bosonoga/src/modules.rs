use std::path::{Path, PathBuf};

use crate::ast::Item;
use crate::sources::Sources;
use crate::{Diagnostic, io_message, parse_file};

const FILE_SUFFIX: &str = ".bosonoga";
const EXPECTED_FILE_SUFFIX: &str = ".xp.bosonoga";

fn is_import_name(name: &str) -> bool {
    name.len() > FILE_SUFFIX.len()
        && !name.starts_with('.')
        && name.ends_with(FILE_SUFFIX)
        && !name.ends_with(EXPECTED_FILE_SUFFIX)
        && !name.contains(['/', '\\'])
}

fn is_already_read(sources: &Sources, name: &str) -> bool {
    for known in sources.files.iter().skip(1) {
        if let Some(known_name) = Path::new(&known.path).file_name()
            && known_name.to_string_lossy() == name
        {
            return true;
        }
    }
    false
}

pub(crate) fn discover(sources: &mut Sources) -> Result<Vec<Vec<usize>>, Diagnostic> {
    let main_path = PathBuf::from(sources.path());
    let directory = match main_path.parent() {
        Some(directory) => directory.to_path_buf(),
        None => PathBuf::new(),
    };
    let program_name = match main_path.file_name() {
        Some(name) => name.to_string_lossy().into_owned(),
        None => String::new(),
    };
    let mut modules = Vec::new();
    let mut read_by: Vec<Option<(usize, String)>> = vec![None];
    let mut index = 0;
    while index < sources.files.len() {
        let mod_names = mod_names(sources, index, &read_by)?;
        let mut indices = Vec::new();
        for (at, name) in mod_names {
            if !is_import_name(&name) {
                return Err(Diagnostic::new(
                    at,
                    "mod takes the name of a .bosonoga file in the directory of the program, as in mod \"lorem.bosonoga\"_str",
                ));
            }
            if name == program_name {
                return Err(Diagnostic::new(at, "a program cannot read itself with mod"));
            }
            if is_already_read(sources, &name) {
                return Err(Diagnostic::new(
                    at,
                    format!("{name} is already read, with mod or io_raw_import"),
                ));
            }
            let path = directory.join(&name).display().to_string();
            let text = match std::fs::read_to_string(&path) {
                Ok(text) => text,
                Err(error) => {
                    return Err(Diagnostic::new(
                        at,
                        format!("cannot read {path}: {}", io_message(&error)),
                    ));
                }
            };
            indices.push(sources.add(path, text));
            read_by.push(Some((at, name)));
        }
        modules.push(indices);
        index += 1;
    }
    Ok(modules)
}

fn mod_names(
    sources: &Sources,
    index: usize,
    read_by: &[Option<(usize, String)>],
) -> Result<Vec<(usize, String)>, Diagnostic> {
    let Some(file) = sources.files.get(index) else {
        return Ok(Vec::new());
    };
    let parsed = parse_file(file)?;
    if parsed.items.is_empty()
        && let Some(Some((at, name))) = read_by.get(index)
    {
        return Err(Diagnostic::new(
            *at,
            format!("{name} has no items, and a module has at least one"),
        ));
    }
    let mut names = Vec::new();
    for item in &parsed.items {
        match item {
            Item::Mod(span, name) => names.push((span.start, name.clone())),
            Item::Const { .. } | Item::Def { .. } | Item::Array { .. } | Item::Section(..) => {}
        }
    }
    Ok(names)
}
