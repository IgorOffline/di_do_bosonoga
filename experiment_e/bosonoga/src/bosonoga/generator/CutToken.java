package bosonoga.generator;

import org.antlr.v4.runtime.CommonToken;
import org.antlr.v4.runtime.Token;

// The CUT where a program is cut at its syntax error, in place of the token of
// the error, which messages show. A VALUE_CUT is where a value could be.
final class CutToken extends CommonToken {
    final Token original;

    CutToken(Token original, int type) {
        super(original);
        setType(type);
        this.original = original;
    }

    static Token original(Token token) {
        return token instanceof CutToken cut ? cut.original : token;
    }
}
