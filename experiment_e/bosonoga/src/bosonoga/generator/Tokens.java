package bosonoga.generator;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import org.antlr.v4.runtime.CharStream;
import org.antlr.v4.runtime.CharStreams;
import org.antlr.v4.runtime.CommonToken;
import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.TokenSource;
import org.antlr.v4.runtime.misc.Pair;

// Making, checking and showing tokens, as every reader of bosonoga source does.
final class Tokens {
    private static final int SHOWN_MAX = 32;

    private Tokens() {
    }

    static BosonogaLexer lexer(String source, String sourceName) {
        var lexer = new BosonogaLexer(CharStreams.fromString(source, sourceName));
        lexer.removeErrorListeners();
        lexer.addErrorListener(ThrowingErrorListener.INSTANCE);
        return lexer;
    }

    // An empty token at the place of at, which errors then name.
    static Token synthetic(int type, Token at, String text) {
        var token = at == null ? new CommonToken(type) : new CommonToken(
                new Pair<TokenSource, CharStream>(at.getTokenSource(), at.getInputStream()), type,
                Token.DEFAULT_CHANNEL, at.getStartIndex(), at.getStartIndex() - 1);
        if (at != null) {
            token.setLine(at.getLine());
            token.setCharPositionInLine(at.getCharPositionInLine());
        }
        token.setText(text);
        return token;
    }

    // The C tokenizer checks a literal as it reads it, before the tokens after it.
    static void checkLiteral(Token token) {
        var type = token.getType();
        if (type == BosonogaLexer.STRING || type == BosonogaLexer.UNTERMINATED_STRING) {
            StringLiteral.check(token);
        } else if (type == BosonogaLexer.NUMBER || type == BosonogaLexer.INDEXED_TYPE) {
            try {
                NumberLiteral.check(token.getText());
            } catch (NumberLiteral.Invalid invalid) {
                throw BosonogaException.at(token, invalid.getMessage());
            }
        }
    }

    static boolean isEnd(Token token) {
        return token.getType() == Token.EOF || token.getType() == BosonogaParser.FILE_END;
    }

    // At most the first 32 bytes, with each byte of a character that is cut
    // shown as U+FFFD, as the C bosonoga shows a token.
    static String shown(Token token) {
        token = CutToken.original(token);
        if (isEnd(token)) {
            return "end of file";
        }
        var bytes = token.getText().getBytes(StandardCharsets.UTF_8);
        if (bytes.length <= SHOWN_MAX) {
            return "'" + token.getText() + "'";
        }
        var end = SHOWN_MAX;
        while (end > 0 && (bytes[end] & 0xC0) == 0x80) {
            end--;
        }
        var text = new String(Arrays.copyOf(bytes, end), StandardCharsets.UTF_8);
        return "'" + text + "�".repeat(SHOWN_MAX - end) + "'";
    }

    // As shown, without its quotes, and empty at the end of a file.
    static String rawShown(Token token) {
        var original = CutToken.original(token);
        if (isEnd(original)) {
            return "";
        }
        var shown = shown(original);
        return shown.substring(1, shown.length() - 1);
    }
}
