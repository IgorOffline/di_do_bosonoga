package bosonoga.generator;

import org.antlr.v4.runtime.ParserRuleContext;

// An error of the tokens, at the token of this index, which the parser found
// in context, or the lexer before the parser saw it.
final class SyntaxFailure extends BosonogaException {
    final int index;
    final ParserRuleContext context;
    // Whether a value could be at the index, where a cut is a value.
    final boolean expectsValue;

    SyntaxFailure(String message, int index, ParserRuleContext context, boolean expectsValue) {
        super(message);
        this.index = index;
        this.context = context;
        this.expectsValue = expectsValue;
    }
}
