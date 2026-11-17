package bosonoga.generator;

import org.antlr.v4.runtime.Token;

public class BosonogaException extends RuntimeException {
    public BosonogaException(String sourceName, int line, int column, String message) {
        super("%s:%d:%d: %s".formatted(sourceName, line, column, message));
    }

    // An error of a whole file, such as regina.ini, starts with its name alone.
    public BosonogaException(String message) {
        super(message);
    }

    // At a token of the file that its lexer read.
    static BosonogaException at(Token token, String message) {
        return new BosonogaException(token.getInputStream().getSourceName(), token.getLine(), Columns.of(token),
                message);
    }
}
