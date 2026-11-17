package bosonoga.generator;

import java.math.BigInteger;

// The C++ of a value, with its kind. An integer literal keeps its value, as the
// size of an array is one.
record Expression(String cpp, Kind kind, BigInteger whole) {
    Expression(String cpp, Kind kind) {
        this(cpp, kind, null);
    }
}
