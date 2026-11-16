package bosonoga.generator;

import org.antlr.v4.runtime.BaseErrorListener;
import org.antlr.v4.runtime.Lexer;
import org.antlr.v4.runtime.NoViableAltException;
import org.antlr.v4.runtime.Parser;
import org.antlr.v4.runtime.RecognitionException;
import org.antlr.v4.runtime.Recognizer;
import org.antlr.v4.runtime.Token;

final class ThrowingErrorListener extends BaseErrorListener {
    static final ThrowingErrorListener INSTANCE = new ThrowingErrorListener();

    private ThrowingErrorListener() {
    }

    @Override
    public void syntaxError(Recognizer<?, ?> recognizer, Object offendingSymbol, int line,
                            int charPositionInLine, String message, RecognitionException cause) {
        // As the C bosonoga, of a character that starts no token.
        if (recognizer instanceof Lexer) {
            var lexer = (Lexer) recognizer;
            throw new BosonogaException(lexer.getInputStream().getSourceName(), line,
                    Columns.at(lexer.getInputStream(), lexer._tokenStartCharIndex), "unexpected character");
        }
        // A token of a file that mod or io_raw_import reads is in that file.
        var token = (Token) offendingSymbol;
        var source = token.getInputStream() != null ? token.getInputStream().getSourceName()
                : recognizer.getInputStream().getSourceName();
        var shown = new BosonogaException(source, line, Columns.of(token), message).getMessage();
        var parser = (Parser) recognizer;
        // A choice that fails past its first token had chosen what came before.
        var isPast = cause instanceof NoViableAltException noViable
                && noViable.getStartToken() != noViable.getOffendingToken();
        throw new SyntaxFailure(shown, token.getTokenIndex(), parser.getContext(),
                !isPast && parser.getExpectedTokens().contains(BosonogaParser.STRING));
    }
}
