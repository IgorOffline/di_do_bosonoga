package bosonoga.generator;

// Check before ANTLR descends further, rather than after building an
// arbitrarily deep tree. Array rows have di/do too, but are not blocks.
final class BlockNesting {
    private static final int MAX = 256;

    private BlockNesting() {
    }

    static void check(BosonogaParser parser) {
        var tokens = parser.getTokenStream();
        // The archive diagnoses an empty block before checking its depth.
        // CUT is also allowed, so the generator can check earlier errors.
        if (tokens.LA(1) != BosonogaParser.DI || tokens.LA(2) == BosonogaParser.DO) {
            return;
        }
        var depth = 0;
        for (var context = parser.getContext().getParent(); context != null; context = context.getParent()) {
            if (context instanceof BosonogaParser.BlockContext) {
                depth++;
            }
        }
        if (depth == MAX) {
            var token = parser.getCurrentToken();
            var message = BosonogaException.at(token, "blocks are nested more than " + MAX + " levels deep")
                    .getMessage();
            throw new SyntaxFailure(message, token.getTokenIndex(), parser.getContext(), false);
        }
    }
}
