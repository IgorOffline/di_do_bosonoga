package bosonoga.generator;

import org.antlr.v4.runtime.CharStreams;
import org.antlr.v4.runtime.CommonTokenStream;

public final class Generator {
    private Generator() {
    }

    // The program's bosonoga_main becomes the C++ function
    // bosonoga::program::<functionName>, which returns its exit status.
    public static String generate(String source, String sourceName, String functionName) {
        var lexer = new BosonogaLexer(CharStreams.fromString(source, sourceName));
        lexer.removeErrorListeners();
        lexer.addErrorListener(ThrowingErrorListener.INSTANCE);

        var tokens = new CommonTokenStream(lexer);
        var parser = new BosonogaParser(tokens);
        parser.removeErrorListeners();
        parser.addErrorListener(ThrowingErrorListener.INSTANCE);

        return new CppEmitter(sourceName, functionName, tokens).visit(parser.program());
    }
}
