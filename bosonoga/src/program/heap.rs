// gfx_heap_bytes: an estimate of the heap that the interpreter owns. It is
// the room reserved by its containers, which are the frames of variables
// (var arrays and the history a program keeps are in them), the rows of the
// constant arrays, the shapes of the scene and the info text not yet shown,
// and the text and boxes that their values own.
//
// No two values share an allocation: a value is copied, never shared, so
// counting what each owner holds counts each allocation once. The allocator's
// own overhead, the graphics library, the GPU and the thread stacks are not
// counted.

use std::mem::size_of;

use super::Array;
use crate::gfx::{Scene, Shape};
use crate::value::Value;

// What a value holds besides its own place: the text of a str, a font or an
// icon, and the box of a present optional.
fn value_heap(value: &Value) -> usize {
    match value {
        Value::Str(text) | Value::Font { text, .. } | Value::Icon { text, .. } => text.capacity(),
        Value::Optional {
            value: Some(inner), ..
        } => size_of::<Value>() + value_heap(inner),
        Value::Optional { value: None, .. }
        | Value::I32(_)
        | Value::I64(_)
        | Value::U32(_)
        | Value::U64(_)
        | Value::F32(_)
        | Value::F64(_)
        | Value::Flags(_)
        | Value::Color(_)
        | Value::Bool(_)
        | Value::InputKey(_) => 0,
    }
}

fn values_heap(values: &Vec<Value>) -> usize {
    let mut total = values.capacity() * size_of::<Value>();
    for value in values {
        total += value_heap(value);
    }
    total
}

pub(super) fn bytes(
    frames: &Vec<Vec<Value>>,
    arrays: &Vec<Array>,
    scene: &Scene,
    output: &String,
) -> u64 {
    let mut total = frames.capacity() * size_of::<Vec<Value>>();
    for frame in frames {
        total += values_heap(frame);
    }
    total += arrays.capacity() * size_of::<Array>();
    for array in arrays {
        total += array.name.capacity() + array.rows.capacity() * size_of::<Vec<Value>>();
        for row in &array.rows {
            total += values_heap(row);
        }
    }
    total += scene.shapes.capacity() * size_of::<Shape>();
    for shape in &scene.shapes {
        if let Shape::Text { text, .. } = shape {
            total += text.capacity();
        }
    }
    total += output.capacity();
    u64::try_from(total).unwrap_or(u64::MAX)
}

#[cfg(test)]
mod tests {
    use std::mem::size_of;

    use super::{bytes, value_heap};
    use crate::gfx::{Align, Horizontal, Scene, Shape, Vertical};
    use crate::program::Array;
    use crate::value::{Kind, Value};

    #[test]
    fn what_a_value_holds() {
        assert_eq!(value_heap(&Value::I64(7)), 0);
        let text = String::with_capacity(100);
        assert_eq!(value_heap(&Value::Str(text)), 100);
        let boxed = Value::present(Value::Str(String::with_capacity(10)));
        assert_eq!(value_heap(&boxed), size_of::<Value>() + 10);
        let absent = Value::Optional {
            inner: Kind::Str,
            value: None,
        };
        assert_eq!(value_heap(&absent), 0);
    }

    #[test]
    fn every_container_once() {
        let mut frame = Vec::with_capacity(4);
        frame.push(Value::Str(String::with_capacity(50)));
        let mut frames = Vec::with_capacity(2);
        frames.push(frame);
        let arrays = vec![Array {
            name: String::with_capacity(3),
            rows: vec![vec![Value::Str(String::with_capacity(7))]],
        }];
        let scene = Scene {
            clear: 0,
            shapes: vec![Shape::Text {
                x: 0.0,
                y: 0.0,
                font: 0,
                size: 1.0,
                color: 0,
                align: Align {
                    horizontal: Horizontal::Left,
                    vertical: Vertical::Baseline,
                },
                text: String::with_capacity(20),
            }],
            mouse_captured: false,
        };
        let expected = 2 * size_of::<Vec<Value>>()
            + 4 * size_of::<Value>()
            + 50
            + size_of::<Array>()
            + 3
            + size_of::<Vec<Value>>()
            + size_of::<Value>()
            + 7
            + size_of::<Shape>()
            + 20
            + 64;
        let found = bytes(&frames, &arrays, &scene, &String::with_capacity(64));
        assert_eq!(found, u64::try_from(expected).unwrap());
    }
}
