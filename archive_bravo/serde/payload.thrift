namespace c_glib Bosonoga

const i32 PAYLOAD_VALUE_COUNT = 12

struct Payload {
  1: required list<i32> i32_values
  2: required list<double> f32_values
}
