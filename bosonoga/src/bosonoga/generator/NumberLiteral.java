package bosonoga.generator;

import java.math.BigInteger;
import java.util.Optional;
import java.util.regex.Pattern;

// A number literal, as in 7_i32 or 1d5_f64, which a program and the group
// global of regina.ini both have.
final class NumberLiteral {
    private static final Pattern NUMBER =
            Pattern.compile("(-?[0-9]+)(?:d([0-9]+))?(?:_([A-Za-z0-9_]*))?");

    // Its message is that of the C bosonoga, and the caller says where it is.
    static final class Invalid extends RuntimeException {
        private Invalid(String message) {
            super(message);
        }
    }

    private NumberLiteral() {
    }

    static Expression parse(String text) {
        var matcher = NUMBER.matcher(text);
        if (!matcher.matches()) {
            throw new IllegalStateException("not a number: " + text);
        }
        var kind = Optional.ofNullable(matcher.group(3)).flatMap(Kind::number)
                .orElseThrow(() -> new Invalid("number must have the suffix _i32, _i64, _u32, _u64, _f32 or _f64"));
        var whole = matcher.group(1);
        var fraction = matcher.group(2);
        var range = "the number is outside the %s range".formatted(kind.shown);
        if (kind.isInteger()) {
            if (fraction != null) {
                throw new Invalid("an integer cannot have a fraction");
            }
            var value = new BigInteger(whole);
            if (!fits(kind, value)) {
                throw new Invalid(range);
            }
            return new Expression(wholeLiteral(kind, value), kind, value);
        }
        var real = Double.parseDouble(whole + "." + (fraction == null ? "0" : fraction));
        // Written so that NaN is outside the range too, as in the C bosonoga.
        if (kind == Kind.F32 ? !(Math.abs(real) <= Float.MAX_VALUE) : !Double.isFinite(real)) {
            throw new Invalid(range);
        }
        // A hexadecimal literal is exact, where a decimal one would be rounded again.
        return new Expression(kind == Kind.F32
                ? Float.toHexString((float) real) + "F"
                : Double.toHexString(real), kind);
    }

    private static boolean fits(Kind kind, BigInteger value) {
        var bits = kind == Kind.I32 || kind == Kind.U32 ? 32 : 64;
        var isSigned = kind == Kind.I32 || kind == Kind.I64;
        var min = isSigned ? BigInteger.ONE.shiftLeft(bits - 1).negate() : BigInteger.ZERO;
        var max = isSigned
                ? BigInteger.ONE.shiftLeft(bits - 1).subtract(BigInteger.ONE)
                : BigInteger.ONE.shiftLeft(bits).subtract(BigInteger.ONE);
        return value.compareTo(min) >= 0 && value.compareTo(max) <= 0;
    }

    private static String wholeLiteral(Kind kind, BigInteger value) {
        return switch (kind) {
            case I32 -> "std::int32_t{" + value + "}";
            // The literal 9223372036854775808 does not fit in a signed type.
            case I64 -> value.equals(BigInteger.valueOf(Long.MIN_VALUE))
                    ? "std::int64_t{-9223372036854775807LL - 1}"
                    : "std::int64_t{" + value + "LL}";
            case U32 -> "std::uint32_t{" + value + "U}";
            case U64 -> "std::uint64_t{" + value + "ULL}";
            default -> throw new IllegalStateException(kind + " is not an integer");
        };
    }
}
