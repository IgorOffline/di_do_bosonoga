use std::thread;
use std::time::{Duration, Instant};

use bosonoga::gfx::Input;
use macroquad::input::utils::repeat_all_miniquad_input;
use macroquad::miniquad::{EventHandler, KeyCode, KeyMods};

use crate::keys;

const FRAME_TIME: Duration = Duration::from_micros(1_000_000 / 60);

#[derive(Default)]
struct Keys {
    pressed: Vec<u32>,
}

impl EventHandler for Keys {
    fn update(&mut self) {}

    fn draw(&mut self) {}

    fn key_down_event(&mut self, keycode: KeyCode, _: KeyMods, repeat: bool) {
        if repeat {
            return;
        }
        if let Some(code) = keys::code(keycode) {
            self.pressed.push(code);
        }
    }
}

// Keeps 60 frames a second, and collects the keys pressed since the last
// frame.
pub(crate) struct Pacer {
    keys: Keys,
    next_frame_time: Instant,
    last_frame_start: Option<Instant>,
}

impl Pacer {
    pub(crate) fn new() -> Pacer {
        Pacer {
            keys: Keys::default(),
            next_frame_time: Instant::now(),
            last_frame_start: None,
        }
    }

    pub(crate) fn wait(&mut self, subscriber: usize) -> Input {
        let now = Instant::now();
        if now < self.next_frame_time {
            thread::sleep(self.next_frame_time - now);
            self.next_frame_time += FRAME_TIME;
        } else {
            self.next_frame_time = now + FRAME_TIME;
        }
        let start = Instant::now();
        let elapsed = match self.last_frame_start {
            Some(last) => start - last,
            None => FRAME_TIME,
        };
        self.last_frame_start = Some(start);
        repeat_all_miniquad_input(&mut self.keys, subscriber);
        Input {
            keys: std::mem::take(&mut self.keys.pressed),
            delta_time: elapsed.as_secs_f32(),
        }
    }
}
