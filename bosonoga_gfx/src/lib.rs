// The gfx scaffold of bosonoga: it runs a gfx program in a window with
// macroquad. bosonoga says what to draw, and this crate draws it.

mod assets;
// Macroquad exposes context diagnostics only through its unsafe GL interface.
#[allow(unsafe_code)]
mod diagnostics;
mod draw;
mod host;
mod keys;
mod pacer;

use std::path::Path;
use std::process::ExitCode;

use bosonoga::settings::{Asset, Settings};
use macroquad::window::Conf;

fn fail(message: &str) -> ExitCode {
    eprintln!("{message}");
    ExitCode::FAILURE
}

fn read_assets(
    path: &str,
    group: &str,
    assets: &[(String, Asset)],
) -> Result<Vec<(String, String, Vec<u8>)>, String> {
    let mut files = Vec::new();
    for (key, asset) in assets {
        let shown = asset.path.display().to_string();
        match std::fs::read(&asset.path) {
            Ok(bytes) => files.push((key.clone(), shown, bytes)),
            Err(error) => return Err(format!("{path}: {group}_{key}: {shown}: {error}")),
        }
    }
    Ok(files)
}

// Runs the gfx program at default_program, or at the one argument of the
// command line, with the settings of settings_file. It returns only when the
// app cannot start; once the window is open, the process exits with the
// status of the program.
pub fn main(settings_file: &str, default_program: &str) -> ExitCode {
    let settings = match Settings::read(Path::new(settings_file)) {
        Ok(settings) => settings,
        Err(message) => return fail(&message),
    };
    let mut arguments = std::env::args().skip(1);
    let mut path = default_program.to_owned();
    if let Some(given) = arguments.next() {
        path = given;
    }
    if arguments.next().is_some() {
        return fail(&format!("usage: {} [FILE]", settings.name));
    }
    if !settings.skip_version_check && settings.version != bosonoga::VERSION {
        return fail(&format!(
            "{} {} needs bosonoga {}, but this is bosonoga {}",
            settings.name,
            settings.version,
            settings.version,
            bosonoga::VERSION
        ));
    }
    let program = match bosonoga::load(Path::new(&path), &settings) {
        Ok(program) => program,
        Err(message) => return fail(&message),
    };
    if !program.is_gfx() {
        return fail(&format!("{path}: a gfx app needs gfx_main"));
    }
    let fonts = match read_assets(&path, "font", program.fonts()) {
        Ok(fonts) => fonts,
        Err(message) => return fail(&message),
    };
    let icons = match read_assets(&path, "icon", program.icons()) {
        Ok(icons) => icons,
        Err(message) => return fail(&message),
    };
    let conf = Conf {
        window_title: path,
        window_width: i32::try_from(settings.gfx_width).unwrap_or(i32::MAX),
        window_height: i32::try_from(settings.gfx_height).unwrap_or(i32::MAX),
        window_resizable: false,
        ..Conf::default()
    };
    macroquad::Window::from_config(
        conf,
        host::run(program, fonts, icons, settings.skip_version_check),
    );
    ExitCode::FAILURE
}
