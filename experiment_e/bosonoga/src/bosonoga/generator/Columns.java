package bosonoga.generator;

import java.nio.charset.StandardCharsets;
import org.antlr.v4.runtime.CharStream;
import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.misc.Interval;

// The C bosonoga counts the column of a line in bytes, where ANTLR counts
// characters, so each column that an error names is counted again here.
final class Columns {
    private Columns() {
    }

    static int of(Token token) {
        var input = token.getInputStream();
        if (input == null || token.getStartIndex() < 0) {
            return token.getCharPositionInLine() + 1;
        }
        return at(input, token.getStartIndex());
    }

    // Of the character at index, which counts the characters of the stream.
    static int at(CharStream input, int index) {
        var start = Math.min(index, input.size());
        while (start > 0 && !input.getText(Interval.of(start - 1, start - 1)).equals("\n")) {
            start--;
        }
        return input.getText(Interval.of(start, index - 1)).getBytes(StandardCharsets.UTF_8).length + 1;
    }
}
