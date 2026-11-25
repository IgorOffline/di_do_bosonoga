use macroquad::miniquad::KeyCode;

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
