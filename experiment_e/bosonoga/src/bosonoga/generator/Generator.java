package bosonoga.generator;

import java.util.ArrayList;
import java.util.List;
import org.antlr.v4.runtime.CommonTokenStream;
import org.antlr.v4.runtime.ListTokenSource;
import org.antlr.v4.runtime.ParserRuleContext;
import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.atn.PredictionMode;

public final class Generator {
    private Generator() {
    }

    // The program's bosonoga_main becomes the C++ function
    // bosonoga::program::<functionName>, which returns its exit status. The
    // settings are those of regina.ini, which Settings.read reads.
    static String generate(String source, String sourceName, String functionName, Settings settings) {
        var importing = new ImportingTokenSource(Tokens.lexer(source, sourceName), sourceName);
        var tokens = new CommonTokenStream(importing);
        BosonogaParser.ProgramContext program;
        try {
            program = parser(tokens).program();
        } catch (SyntaxFailure failure) {
            return generateBefore(failure, tokens.getTokens(), sourceName, functionName, importing, settings);
        }
        return new CppEmitter(sourceName, functionName, tokens, importing, settings).visit(program);
    }

    private static BosonogaParser parser(CommonTokenStream tokens) {
        var parser = new BosonogaParser(tokens);
        parser.removeErrorListeners();
        parser.addErrorListener(ThrowingErrorListener.INSTANCE);
        parser.setErrorHandler(new BosonogaErrorStrategy());
        // As the C bosonoga reads each token in its context, the grammar's
        // choices look at what follows, which SLL prediction takes as anything.
        parser.getInterpreter().setPredictionMode(PredictionMode.LL);
        return parser;
    }

    // The C bosonoga finds errors in the order of the program, so the program
    // is generated again up to the statement or item of the syntax error,
    // whose CUT throws it if nothing before it fails.
    private static String generateBefore(SyntaxFailure failure, List<Token> read, String sourceName,
                                         String functionName, ImportingTokenSource importing, Settings settings) {
        var before = new ArrayList<>(read.subList(0, Math.min(failure.index, read.size())));
        before.removeIf(token -> token.getType() == Token.EOF);
        var found = failure.context != null ? failure : failureAt(before);
        var context = found == null ? null : found.context;
        var cutType = found != null && found.expectsValue ? BosonogaParser.VALUE_CUT : BosonogaParser.CUT;
        var isEnd = failure.index < read.size() && (read.get(failure.index).getType() == Token.EOF
                || read.get(failure.index).getType() == BosonogaParser.FILE_END);
        var at = failure.index < read.size() ? read.get(failure.index) : read.getLast();
        var isBlockEnd = isEnd && context != null && cutIndex(context, failure.index) == failure.index
                && balance(before, BosonogaParser.DI, BosonogaParser.DO) > 0;
        // First at the token itself, where the grammar takes a CUT, as then
        // the generator checks what is before it in the statement.
        if (!isBlockEnd) {
            var program = parsed(closed(before, at, false, cutType), sourceName);
            if (program != null) {
                emit(program, failure, sourceName, functionName, importing, settings);
            }
        }
        var cut = cutIndex(context, failure.index);
        var program = parsed(closed(new ArrayList<>(before.subList(0, Math.min(cut, before.size()))), at,
                isBlockEnd, BosonogaParser.CUT), sourceName);
        if (program == null) {
            throw failure;
        }
        emit(program, failure, sourceName, functionName, importing, settings);
        throw failure;
    }

    private record Parsed(BosonogaParser.ProgramContext program, CommonTokenStream tokens) {
    }

    private static void emit(Parsed parsed, SyntaxFailure failure, String sourceName, String functionName,
                             ImportingTokenSource importing, Settings settings) {
        new CppEmitter(sourceName, functionName, parsed.tokens(), importing, settings, failure)
                .visit(parsed.program());
        throw failure;
    }

    private static Parsed parsed(List<Token> tokens, String sourceName) {
        var stream = new CommonTokenStream(new ListTokenSource(tokens, sourceName));
        try {
            return new Parsed(parser(stream).program(), stream);
        } catch (SyntaxFailure again) {
            return null;
        }
    }

    // The tokens before the cut, the CUT, and the do and file ends that close
    // what is open there.
    private static List<Token> closed(List<Token> before, Token at, boolean isBlockEnd, int cutType) {
        var tokens = new ArrayList<>(before);
        var open = balance(tokens, BosonogaParser.DI, BosonogaParser.DO);
        // At the end of a file in a block, its block ends first, as its
        // variables are checked before its do is.
        if (isBlockEnd) {
            tokens.add(synthetic(BosonogaParser.DO, at));
            open--;
        }
        tokens.add(new CutToken(at, cutType));
        for (int i = 0; i < open; i++) {
            tokens.add(synthetic(BosonogaParser.DO, at));
        }
        for (int i = balance(tokens, BosonogaParser.FILE_START, BosonogaParser.FILE_END); i > 0; i--) {
            tokens.add(synthetic(BosonogaParser.FILE_END, at));
        }
        tokens.add(synthetic(Token.EOF, at));
        return tokens;
    }

    // Where the parser is when it finds a token that the lexer refused.
    private static SyntaxFailure failureAt(List<Token> before) {
        var tokens = new ArrayList<>(before);
        tokens.add(synthetic(Token.INVALID_TYPE, before.isEmpty() ? null : before.getLast()));
        try {
            parser(new CommonTokenStream(new ListTokenSource(tokens))).program();
        } catch (SyntaxFailure failure) {
            return failure;
        }
        return null;
    }

    // A statement is cut at its start, and an error between statements, or
    // between items, where it is.
    private static int cutIndex(ParserRuleContext context, int index) {
        for (var at = context; at != null; at = at.getParent()) {
            if (at instanceof BosonogaParser.BlockContext || at instanceof BosonogaParser.ProgramContext
                    || at instanceof BosonogaParser.FileItemContext) {
                return index;
            }
            if (at instanceof BosonogaParser.StatementContext || at instanceof BosonogaParser.ItemContext) {
                return at.getStart() != null ? Math.min(at.getStart().getTokenIndex(), index) : index;
            }
        }
        return index;
    }

    // How many of the tokens open what is not closed among them.
    private static int balance(List<Token> tokens, int open, int close) {
        var balance = 0;
        for (var token : tokens) {
            balance += token.getType() == open ? 1 : token.getType() == close ? -1 : 0;
        }
        return balance;
    }

    private static Token synthetic(int type, Token at) {
        return Tokens.synthetic(type, at, type == Token.EOF ? "<EOF>" : "");
    }
}
