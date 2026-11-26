use std::thread;
use std::time::{Duration, Instant};

use bosonoga::gfx::{Input, KeyEvent};
use macroquad::input::utils::repeat_all_miniquad_input;
use macroquad::input::{mouse_position, set_cursor_grab, show_mouse};
use macroquad::miniquad::{EventHandler, KeyCode, KeyMods};

use crate::key;

// The keys since the last frame. A key comes up only after it went down, so
// that the program sees them in pairs, and the keys that are down all come
// up when the window loses focus, which miniquad reports as minimized.
#[derive(Default)]
struct Keys {
    events: Vec<KeyEvent>,
    // The keys that are down, in the order they went down.
    down: Vec<u32>,
    focus_lost: bool,
}

impl EventHandler for Keys {
    fn update(&mut self) {}

    fn draw(&mut self) {}

    fn key_down_event(&mut self, keycode: KeyCode, _: KeyMods, repeat: bool) {
        if repeat {
            return;
        }
        if let Some(code) = key::code(keycode)
            && !self.down.contains(&code)
        {
            self.down.push(code);
            self.events.push(KeyEvent::Down(code));
        }
    }

    fn key_up_event(&mut self, keycode: KeyCode, _: KeyMods) {
        if let Some(code) = key::code(keycode)
            && let Some(at) = self.down.iter().position(|down| *down == code)
        {
            self.down.remove(at);
            self.events.push(KeyEvent::Up(code));
        }
    }

    fn window_minimized_event(&mut self) {
        self.focus_lost = true;
        for code in self.down.drain(..) {
            self.events.push(KeyEvent::Up(code));
        }
    }
}

// The capture of the mouse, and its moves while captured.
struct Mouse {
    captured: bool,
    // Whether it was captured or released in this frame.
    changed: bool,
    last: (f32, f32),
}

impl Mouse {
    fn set(&mut self, captured: bool) {
        set_cursor_grab(captured);
        show_mouse(!captured);
        self.captured = captured;
        self.changed = true;
    }

    // The pixels moved since the last frame, which are 0 while released and
    // in a frame that captured or released the mouse, as the jump from where
    // the cursor was is no move of the player's.
    fn moved(&mut self) -> [f32; 2] {
        let now = mouse_position();
        let (x, y) = (now.0 - self.last.0, now.1 - self.last.1);
        self.last = now;
        let still = !self.captured || self.changed;
        self.changed = false;
        if still { [0.0, 0.0] } else { [x, y] }
    }
}

// The time a frame lasts at fps_max frames a second, to the nanosecond, as
// 1_000_000 / 60 microseconds would round 60 to 60.0024.
fn frame_time(fps_max: u32) -> Duration {
    Duration::from_secs(1) / fps_max.max(1)
}

// Keeps at most fps_max frames a second, and collects the keys and the mouse
// since the last frame. When vsync holds the window to a lower rate, the wait
// is already over and it does not sleep.
pub(crate) struct Pacer {
    keys: Keys,
    mouse: Mouse,
    frame_time: Duration,
    next_frame_time: Instant,
    last_frame_start: Option<Instant>,
}

impl Pacer {
    pub(crate) fn new(fps_max: u32) -> Pacer {
        Pacer {
            keys: Keys::default(),
            mouse: Mouse {
                captured: false,
                changed: false,
                last: mouse_position(),
            },
            frame_time: frame_time(fps_max),
            next_frame_time: Instant::now(),
            last_frame_start: None,
        }
    }

    // Captures or releases the mouse as the scene just shown asks, and waits
    // for the next frame.
    pub(crate) fn wait(&mut self, subscriber: usize, mouse_captured: bool) -> Input {
        if mouse_captured != self.mouse.captured {
            self.mouse.set(mouse_captured);
        }
        let now = Instant::now();
        if now < self.next_frame_time {
            thread::sleep(self.next_frame_time - now);
            self.next_frame_time += self.frame_time;
        } else {
            self.next_frame_time = now + self.frame_time;
        }
        let start = Instant::now();
        let elapsed = match self.last_frame_start {
            Some(last) => start - last,
            None => self.frame_time,
        };
        self.last_frame_start = Some(start);
        repeat_all_miniquad_input(&mut self.keys, subscriber);
        if std::mem::take(&mut self.keys.focus_lost) && self.mouse.captured {
            self.mouse.set(false);
        }
        Input {
            keys: std::mem::take(&mut self.keys.events),
            delta_time: elapsed.as_secs_f32(),
            mouse_move: self.mouse.moved(),
            mouse_captured: self.mouse.captured,
        }
    }

    // Gives the cursor back before the app exits.
    pub(crate) fn release_mouse(&mut self) {
        if self.mouse.captured {
            self.mouse.set(false);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{Keys, frame_time};
    use std::time::Duration;
    use bosonoga::gfx::KeyEvent;
    use macroquad::miniquad::{EventHandler, KeyCode, KeyMods};

    fn code(name: &str) -> u32 {
        bosonoga::key::named(name).unwrap()
    }

    // A frame lasts a whole second over fps_max, to the nanosecond.
    #[test]
    fn frame_time_is_exact() {
        assert_eq!(frame_time(60), Duration::from_nanos(16_666_666));
        assert_eq!(frame_time(144), Duration::from_nanos(6_944_444));
        assert_eq!(frame_time(1), Duration::from_secs(1));
    }

    // Each key that comes up went down before, a second down of a key that
    // is down is not another press, and losing focus brings up every key that
    // is down, in the order they went down.
    #[test]
    fn keys_come_up_in_pairs() {
        let mods = KeyMods::default();
        let mut keys = Keys::default();
        keys.key_up_event(KeyCode::S, mods);
        keys.key_down_event(KeyCode::W, mods, false);
        keys.key_down_event(KeyCode::W, mods, true);
        keys.key_down_event(KeyCode::W, mods, false);
        keys.key_down_event(KeyCode::A, mods, false);
        keys.key_up_event(KeyCode::W, mods);
        keys.key_up_event(KeyCode::W, mods);
        keys.key_down_event(KeyCode::D, mods, false);
        keys.key_down_event(KeyCode::W, mods, false);
        assert!(!keys.focus_lost);
        keys.window_minimized_event();
        let (w, a, d) = (
            code("SAPP_KEYCODE_W"),
            code("SAPP_KEYCODE_A"),
            code("SAPP_KEYCODE_D"),
        );
        assert_eq!(
            keys.events,
            [
                KeyEvent::Down(w),
                KeyEvent::Down(a),
                KeyEvent::Up(w),
                KeyEvent::Down(d),
                KeyEvent::Down(w),
                KeyEvent::Up(a),
                KeyEvent::Up(d),
                KeyEvent::Up(w),
            ]
        );
        assert!(keys.focus_lost && keys.down.is_empty());
    }
}
