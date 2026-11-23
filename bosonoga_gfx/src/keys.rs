use macroquad::miniquad::KeyCode;

// The names of bosonoga's keys are sokol_app's, and miniquad's are its
// variants in CamelCase, as KeyCode::PageUp for SAPP_KEYCODE_PAGE_UP.
pub(crate) fn code(keycode: KeyCode) -> Option<u32> {
    let variant = format!("{keycode:?}");
    let word = variant.strip_prefix("Key").unwrap_or(&variant);
    let mut name = String::from("SAPP_KEYCODE_");
    let mut previous: Option<char> = None;
    for letter in word.chars() {
        let after_lowercase = match previous {
            Some(before) => before.is_ascii_lowercase(),
            None => false,
        };
        let starts_word =
            letter.is_ascii_uppercase() || (letter.is_ascii_digit() && after_lowercase);
        if starts_word && previous.is_some() {
            name.push('_');
        }
        name.push(letter.to_ascii_uppercase());
        previous = Some(letter);
    }
    bosonoga::key::named(&name)
}

#[cfg(test)]
mod tests {
    use super::code;
    use macroquad::miniquad::KeyCode;

    // Every variant of miniquad 0.4.11's KeyCode, in its order.
    const ALL: [KeyCode; 122] = [
        KeyCode::Space,
        KeyCode::Apostrophe,
        KeyCode::Comma,
        KeyCode::Minus,
        KeyCode::Period,
        KeyCode::Slash,
        KeyCode::Key0,
        KeyCode::Key1,
        KeyCode::Key2,
        KeyCode::Key3,
        KeyCode::Key4,
        KeyCode::Key5,
        KeyCode::Key6,
        KeyCode::Key7,
        KeyCode::Key8,
        KeyCode::Key9,
        KeyCode::Semicolon,
        KeyCode::Equal,
        KeyCode::A,
        KeyCode::B,
        KeyCode::C,
        KeyCode::D,
        KeyCode::E,
        KeyCode::F,
        KeyCode::G,
        KeyCode::H,
        KeyCode::I,
        KeyCode::J,
        KeyCode::K,
        KeyCode::L,
        KeyCode::M,
        KeyCode::N,
        KeyCode::O,
        KeyCode::P,
        KeyCode::Q,
        KeyCode::R,
        KeyCode::S,
        KeyCode::T,
        KeyCode::U,
        KeyCode::V,
        KeyCode::W,
        KeyCode::X,
        KeyCode::Y,
        KeyCode::Z,
        KeyCode::LeftBracket,
        KeyCode::Backslash,
        KeyCode::RightBracket,
        KeyCode::GraveAccent,
        KeyCode::World1,
        KeyCode::World2,
        KeyCode::Escape,
        KeyCode::Enter,
        KeyCode::Tab,
        KeyCode::Backspace,
        KeyCode::Insert,
        KeyCode::Delete,
        KeyCode::Right,
        KeyCode::Left,
        KeyCode::Down,
        KeyCode::Up,
        KeyCode::PageUp,
        KeyCode::PageDown,
        KeyCode::Home,
        KeyCode::End,
        KeyCode::CapsLock,
        KeyCode::ScrollLock,
        KeyCode::NumLock,
        KeyCode::PrintScreen,
        KeyCode::Pause,
        KeyCode::F1,
        KeyCode::F2,
        KeyCode::F3,
        KeyCode::F4,
        KeyCode::F5,
        KeyCode::F6,
        KeyCode::F7,
        KeyCode::F8,
        KeyCode::F9,
        KeyCode::F10,
        KeyCode::F11,
        KeyCode::F12,
        KeyCode::F13,
        KeyCode::F14,
        KeyCode::F15,
        KeyCode::F16,
        KeyCode::F17,
        KeyCode::F18,
        KeyCode::F19,
        KeyCode::F20,
        KeyCode::F21,
        KeyCode::F22,
        KeyCode::F23,
        KeyCode::F24,
        KeyCode::F25,
        KeyCode::Kp0,
        KeyCode::Kp1,
        KeyCode::Kp2,
        KeyCode::Kp3,
        KeyCode::Kp4,
        KeyCode::Kp5,
        KeyCode::Kp6,
        KeyCode::Kp7,
        KeyCode::Kp8,
        KeyCode::Kp9,
        KeyCode::KpDecimal,
        KeyCode::KpDivide,
        KeyCode::KpMultiply,
        KeyCode::KpSubtract,
        KeyCode::KpAdd,
        KeyCode::KpEnter,
        KeyCode::KpEqual,
        KeyCode::LeftShift,
        KeyCode::LeftControl,
        KeyCode::LeftAlt,
        KeyCode::LeftSuper,
        KeyCode::RightShift,
        KeyCode::RightControl,
        KeyCode::RightAlt,
        KeyCode::RightSuper,
        KeyCode::Menu,
        KeyCode::Back,
        KeyCode::Unknown,
    ];

    // Back, Android's back button, and Unknown have no name in bosonoga.
    const UNNAMED: [KeyCode; 2] = [KeyCode::Back, KeyCode::Unknown];

    #[test]
    fn every_bosonoga_key_once() {
        let mut codes = Vec::new();
        for keycode in ALL {
            match code(keycode) {
                Some(found) => codes.push(found),
                None => assert!(UNNAMED.contains(&keycode), "{keycode:?} has no name"),
            }
        }
        let mut expected = Vec::new();
        let mut index = 0;
        while let Some(name) = bosonoga::key::name(index) {
            expected.push(bosonoga::key::named(name).unwrap());
            index += 1;
        }
        assert_eq!(codes, expected);
    }

    #[test]
    fn names_of_words_and_digits() {
        let pairs = [
            (KeyCode::Key0, "SAPP_KEYCODE_0"),
            (KeyCode::Q, "SAPP_KEYCODE_Q"),
            (KeyCode::PageUp, "SAPP_KEYCODE_PAGE_UP"),
            (KeyCode::World1, "SAPP_KEYCODE_WORLD_1"),
            (KeyCode::F12, "SAPP_KEYCODE_F12"),
            (KeyCode::Kp0, "SAPP_KEYCODE_KP_0"),
            (KeyCode::KpEnter, "SAPP_KEYCODE_KP_ENTER"),
            (KeyCode::Escape, "SAPP_KEYCODE_ESCAPE"),
        ];
        for (keycode, name) in pairs {
            assert_eq!(code(keycode), bosonoga::key::named(name), "{keycode:?}");
            assert!(code(keycode).is_some(), "{keycode:?}");
        }
    }
}
