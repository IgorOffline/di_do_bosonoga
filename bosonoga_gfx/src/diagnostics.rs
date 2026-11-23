use std::ffi::CStr;

use macroquad::window::{get_internal_gl, miniquad};
use miniquad::Backend;
use miniquad::gl::{self, GLenum};

// Core OpenGL selector omitted from miniquad's exported constants.
const GL_RENDERER: GLenum = 0x1F01;

// Called once at the start of host::run, on the window thread with the
// rendering context current, before assets or the interpreter are started.
pub(super) fn print() {
    println!(
        "Bosonoga {} - built with {}",
        bosonoga::VERSION,
        env!("BOSONOGA_RUSTC_VERSION")
    );
    // SAFETY: Macroquad has initialized its context before polling host::run.
    // The temporary context reference is used only to copy its information;
    // no rendering or other Macroquad access occurs while it is borrowed.
    let info = unsafe { get_internal_gl().quad_context.info() };
    match info.backend {
        Backend::OpenGl => {
            // SAFETY: The active backend is OpenGL and its context is current
            // on this thread. These are read-only, core GL string queries.
            let (vendor, renderer, shading_language) = unsafe {
                (
                    gl_string(gl::GL_VENDOR),
                    gl_string(GL_RENDERER),
                    gl_string(gl::GL_SHADING_LANGUAGE_VERSION),
                )
            };
            println!(
                "OpenGL {} - {vendor} - {renderer}\nGLSL {shading_language}",
                info.gl_version_string
            );
        }
        Backend::Metal => println!("Rendering backend: Metal"),
    }
}

// Requires a current OpenGL context and a valid glGetString selector.
unsafe fn gl_string(name: GLenum) -> String {
    // SAFETY: The caller guarantees the context and selector are valid.
    let value = unsafe { gl::glGetString(name) };
    if value.is_null() {
        return "unavailable".to_owned();
    }
    // SAFETY: For these selectors GL returns a null-terminated string owned
    // by the context. Copy it immediately without retaining the pointer.
    unsafe { CStr::from_ptr(value.cast()) }
        .to_string_lossy()
        .into_owned()
}
