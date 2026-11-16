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
    COLOR("color", "runtime::Color"),
    STR("str", "std::string"),
    BOOL("bool", "bool"),
    INPUT_KEY("input_key", "runtime::InputKey"),
    FONT("font", "runtime::Font"),
    ICON("icon", "runtime::Icon"),
    // As a str, but its text is XML, which the generator checks of a literal.
    XML_STR("xml_str", "std::string"),
    DIR("dir", "runtime::Dir");

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

    boolean isText() {
        return this == STR || this == XML_STR;
    }

    // A value that regina.ini names, which has no zero.
    boolean isNamed() {
        return this == INPUT_KEY || this == FONT || this == ICON || this == DIR;
    }

    boolean isNumber() {
        return isInteger() || isReal();
    }

    static Optional<Kind> named(String shown) {
        return Arrays.stream(values()).filter(kind -> kind.shown.equals(shown)).findFirst();
    }

    static Optional<Kind> number(String shown) {
        return Arrays.stream(values()).filter(kind -> kind.isNumber() && kind.shown.equals(shown))
                .findFirst();
    }
}
