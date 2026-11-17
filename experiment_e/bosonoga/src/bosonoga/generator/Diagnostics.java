package bosonoga.generator;

import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.TokenStream;

// The errors of CppEmitter. When Generator replays a program cut at its syntax
// error, an error after the cut is that syntax error, which the C bosonoga
// finds first, and one at the cut is what the C bosonoga finds of the token
// there.
final class Diagnostics {
    private final TokenStream tokens;
    private final Locations locations;
    // The syntax error of a replayed program, or null.
    private final BosonogaException cut;

    Diagnostics(TokenStream tokens, Locations locations, BosonogaException cut) {
        this.tokens = tokens;
        this.locations = locations;
        this.cut = cut;
    }

    BosonogaException cut() {
        return cut;
    }

    BosonogaException error(Token token, String message) {
        // A lexer error is at a token that the lexer never gave.
        var isLexical = cut instanceof SyntaxFailure failure && failure.context == null;
        if (cut != null && (token.getTokenIndex() > cutIndex() || isLexical && token.getTokenIndex() == cutIndex())) {
            return cut;
        }
        return at(token, message);
    }

    // An error that the C bosonoga finds at the token where the program is cut.
    BosonogaException errorAtCut(Token token, String message) {
        if (cut instanceof SyntaxFailure failure && failure.context == null
                && token.getTokenIndex() == cutIndex()) {
            return cut;
        }
        return at(token, message);
    }

    private BosonogaException at(Token token, String message) {
        return new BosonogaException(locations.path(token), token.getLine(), Columns.of(token), message);
    }

    private int cutIndex() {
        for (int i = 0; i < tokens.size(); i++) {
            if (tokens.get(i) instanceof CutToken) {
                return i;
            }
        }
        return Integer.MAX_VALUE;
    }
}
