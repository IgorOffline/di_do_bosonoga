package bosonoga.generator;

import java.util.List;
import java.util.function.UnaryOperator;
import org.antlr.v4.runtime.Token;

// What a program declares, as CppEmitter keeps it while it reads the program.
final class Symbols {
    private Symbols() {
    }

    static final class Variable {
        final Token declaration;
        final Kind kind;
        final boolean isVal;
        final boolean isNew;
        final int loopDepth;
        final String cpp;
        // What gives a value to a block, as xml_child, which frees it after the block.
        final String freedBy;
        final Modules.Home home;
        // The rows of a var array, which is a variable of its block.
        Function array;
        boolean isRead;

        Variable(Token declaration, Kind kind, boolean isVal, boolean isNew, int loopDepth, String cpp,
                 String freedBy, Modules.Home home) {
            this.declaration = declaration;
            this.kind = kind;
            this.isVal = isVal;
            this.isNew = isNew;
            this.loopDepth = loopDepth;
            this.cpp = cpp;
            this.freedBy = freedBy;
            this.home = home;
        }

        // As the C bosonoga names what new made, or what a block is given.
        String made() {
            return freedBy != null ? "taken from a block" : "new";
        }
    }

    // A constant is a literal, which is written where the constant is read.
    static final class Constant {
        final Token declaration;
        final Expression value;
        boolean isUsed;

        Constant(Token declaration, Expression value) {
            this.declaration = declaration;
            this.value = value;
        }
    }

    // A function, or an array, which the C bosonoga reads as a function of its
    // index, so an array has one i32 parameter and its columns as results.
    static final class Function {
        private final Token name;
        private final List<Kind> parameters;
        private final List<Kind> results;
        private final String cpp;
        private final boolean isArray;
        private final int rowCount;
        private boolean isCalled;

        Function(Token name, List<Kind> parameters, List<Kind> results, String cpp, boolean isArray, int rowCount) {
            this.name = name;
            this.parameters = parameters;
            this.results = results;
            this.cpp = cpp;
            this.isArray = isArray;
            this.rowCount = rowCount;
        }

        Token name() {
            return name;
        }

        List<Kind> parameters() {
            return parameters;
        }

        List<Kind> results() {
            return results;
        }

        String cpp() {
            return cpp;
        }

        boolean isArray() {
            return isArray;
        }

        int rowCount() {
            return rowCount;
        }

        // An array is called when it is read, and a function that only calls
        // itself is not.
        boolean isCalled() {
            return isCalled;
        }

        void markCalled() {
            isCalled = true;
        }

        String gives() {
            return isArray ? "has rows of" : "returns";
        }
    }

    // A type with its index, as 0_i32 names the first value of a function.
    record Indexed(int index, Kind kind) {
    }

    // A variable that a block starts with, as the variable of a loop, whose line
    // is made from its C++ name.
    record Declared(Token name, Kind kind, String freedBy, UnaryOperator<String> line) {
        Declared(Token name, Kind kind, UnaryOperator<String> line) {
            this(name, kind, null, line);
        }
    }
}
