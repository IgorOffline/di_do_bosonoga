package bosonoga.generator;

import java.util.Arrays;
import java.util.Optional;

// The types of bosonoga values, and the C++ type each one becomes.
enum Kind {
    I32("i32", "std::int32_t"),
    I64("i64", "std::int64_t"),
    U32("u32", "std::uint32_t"),
    U64("u64", "std::uint64_t"),
    F32("f32", "float"),
    F64("f64", "double"),
    FLAGS("flags", "std::uint32_t"),
    BOOL("bool", "bool"),
    STR("str", "std::string");

    final String shown;
    final String cpp;

    Kind(String shown, String cpp) {
        this.shown = shown;
        this.cpp = cpp;
    }

    boolean isInteger() {
        return this == I32 || this == I64 || this == U32 || this == U64;
    }

    boolean isReal() {
        return this == F32 || this == F64;
    }

    boolean isNumber() {
        return isInteger() || isReal();
    }

    static Optional<Kind> number(String shown) {
        return Arrays.stream(values()).filter(kind -> kind.isNumber() && kind.shown.equals(shown))
                .findFirst();
    }
}
