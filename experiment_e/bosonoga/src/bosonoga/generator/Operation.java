package bosonoga.generator;

import java.util.Arrays;

// The conversions and math functions of var ... is and set, as in the C bosonoga.
enum Operation {
    TO_I32("to_i32", Kind.I32),
    TO_I64("to_i64", Kind.I64),
    TO_U32("to_u32", Kind.U32),
    TO_U64("to_u64", Kind.U64),
    TO_F32("to_f32", Kind.F32),
    TO_F64("to_f64", Kind.F64),
    MATH_TO_ABS("math_to_abs", "abs", false, true),
    MATH_TO_MIN("math_to_min", "min", true, true),
    MATH_TO_MAX("math_to_max", "max", true, true),
    MATH_TO_SIN("math_to_sin", "sin", false, false),
    MATH_TO_COS("math_to_cos", "cos", false, false),
    MATH_TO_TAN("math_to_tan", "tan", false, false),
    MATH_TO_ASIN("math_to_asin", "asin", false, false),
    MATH_TO_ACOS("math_to_acos", "acos", false, false),
    MATH_TO_ATAN2("math_to_atan2", "atan2", true, false),
    MATH_TO_SQRT("math_to_sqrt", "sqrt", false, false),
    MATH_TO_POW("math_to_pow", "pow", true, false),
    MATH_TO_HYPOT("math_to_hypot", "hypot", true, false),
    MATH_TO_FLOOR("math_to_floor", "floor", false, false),
    MATH_TO_CEIL("math_to_ceil", "ceil", false, false),
    MATH_TO_ROUND("math_to_round", "round", false, false),
    MATH_TO_LOG("math_to_log", "log", false, false),
    MATH_TO_LOG10("math_to_log10", "log10", false, false),
    MATH_TO_LOG1P("math_to_log1p", "log1p", false, false),
    MATH_TO_EXP("math_to_exp", "exp", false, false),
    MATH_TO_EXPM1("math_to_expm1", "expm1", false, false);

    final String word;
    // The kind a conversion gives, or null for a math function, which gives the
    // kind of its values.
    final Kind convertsTo;
    // The enumerator of runtime::Math, or null for a conversion.
    final String math;
    final boolean isBinary;
    final boolean acceptsInteger;

    Operation(String word, Kind convertsTo) {
        this.word = word;
        this.convertsTo = convertsTo;
        this.math = null;
        this.isBinary = false;
        this.acceptsInteger = true;
    }

    Operation(String word, String math, boolean isBinary, boolean acceptsInteger) {
        this.word = word;
        this.convertsTo = null;
        this.math = math;
        this.isBinary = isBinary;
        this.acceptsInteger = acceptsInteger;
    }

    Kind result(Kind source) {
        return convertsTo != null ? convertsTo : source;
    }

    static Operation of(String word) {
        return Arrays.stream(values()).filter(operation -> operation.word.equals(word)).findFirst()
                .orElseThrow(() -> new IllegalStateException("no operation " + word));
    }
}
