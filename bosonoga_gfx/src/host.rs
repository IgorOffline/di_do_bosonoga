use std::sync::mpsc::{Receiver, Sender, channel};
use std::thread;

use bosonoga::Program;
use bosonoga::ast::ExitStatus;
use bosonoga::gfx::{Input, Scene, Window};
use macroquad::input::utils::register_input_subscriber;
use macroquad::input::{is_quit_requested, prevent_quit};
use macroquad::window::next_frame;

use crate::assets::Assets;
use crate::draw;
use crate::pacer::Pacer;

const INTERPRETER_STACK: usize = 1 << 28;

enum Event {
    Info(String),
    Frame(Scene),
    Done(Result<ExitStatus, String>),
}

struct Channel {
    events: Sender<Event>,
    inputs: Receiver<Option<Input>>,
}

impl Window for Channel {
    fn info(&mut self, text: &str) {
        let _ = self.events.send(Event::Info(text.to_owned()));
    }

    fn show(&mut self, scene: &Scene) -> Option<Input> {
        if self.events.send(Event::Frame(scene.clone())).is_err() {
            return None;
        }
        self.inputs.recv().unwrap_or(None)
    }
}

fn exit_code(status: ExitStatus) -> u8 {
    match status {
        ExitStatus::Success => 0,
        ExitStatus::Failure => 1,
    }
}

// The interpreter runs on a thread of its own, and every frame it shows is
// drawn here, on the thread of the window.
pub(crate) async fn run(
    program: Program,
    fonts: Vec<(String, String, Vec<u8>)>,
    icons: Vec<(String, String, Vec<u8>)>,
) {
    let assets = match Assets::load(program.path(), &fonts, &icons) {
        Ok(assets) => assets,
        Err(message) => {
            eprintln!("{message}");
            std::process::exit(1);
        }
    };
    prevent_quit();
    let subscriber = register_input_subscriber();
    let (events, received) = channel();
    let (inputs, input_receiver) = channel();
    let done = events.clone();
    let started = thread::Builder::new()
        .stack_size(INTERPRETER_STACK)
        .spawn(move || {
            let mut window = Channel {
                events,
                inputs: input_receiver,
            };
            let result = bosonoga::gfx::run(&program, &mut window);
            let _ = done.send(Event::Done(result));
        });
    if let Err(error) = started {
        eprintln!("cannot start the interpreter: {error}");
        std::process::exit(1);
    }
    let mut pacer = Pacer::new();
    loop {
        match received.recv() {
            Ok(Event::Info(text)) => print!("{text}"),
            Ok(Event::Frame(scene)) => {
                draw::scene(&scene, &assets);
                next_frame().await;
                let input = pacer.wait(subscriber);
                if is_quit_requested() {
                    let _ = inputs.send(None);
                } else {
                    let _ = inputs.send(Some(input));
                }
            }
            Ok(Event::Done(Ok(status))) => std::process::exit(exit_code(status).into()),
            Ok(Event::Done(Err(message))) => {
                eprintln!("{message}");
                std::process::exit(1);
            }
            Err(_) => {
                eprintln!("the interpreter stopped without a result");
                std::process::exit(1);
            }
        }
    }
}
