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
