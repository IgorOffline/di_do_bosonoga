package bosonoga.generator;

import org.antlr.v4.runtime.DefaultErrorStrategy;
import org.antlr.v4.runtime.InputMismatchException;
import org.antlr.v4.runtime.NoViableAltException;
import org.antlr.v4.runtime.Parser;
import org.antlr.v4.runtime.ParserRuleContext;
import org.antlr.v4.runtime.RecognitionException;
import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.misc.IntervalSet;

// The syntax errors that the C bosonoga says in its own words, where the
// grammar alone finds them. Others keep the words of ANTLR.
final class BosonogaErrorStrategy extends DefaultErrorStrategy {
    private static final String TYPES = "i32, i64, u32, u64, f32, f64, flags, color, str or bool";

    @Override
    public void reportError(Parser parser, RecognitionException exception) {
        if (inErrorRecoveryMode(parser)) {
            return;
        }
        var token = exception.getOffendingToken();
        if (exception instanceof NoViableAltException noViable && noViable.getStartToken() != token) {
            var message = pastMessage(parser.getContext(), noViable.getStartToken(), token);
            if (message != null) {
                beginErrorCondition(parser);
                parser.notifyErrorListeners(token, message, exception);
                return;
            }
        }
        if ((exception instanceof NoViableAltException || exception instanceof InputMismatchException)
                && report(parser, token, exception)) {
            return;
        }
        super.reportError(parser, exception);
    }

    @Override
    protected void reportUnwantedToken(Parser parser) {
        if (!inErrorRecoveryMode(parser) && report(parser, parser.getCurrentToken(), null)) {
            return;
        }
        super.reportUnwantedToken(parser);
    }

    @Override
    protected void reportMissingToken(Parser parser) {
        if (!inErrorRecoveryMode(parser) && report(parser, parser.getCurrentToken(), null)) {
            return;
        }
        super.reportMissingToken(parser);
    }

    private boolean report(Parser parser, Token token, RecognitionException exception) {
        var message = message(parser, parser.getContext(), token);
        if (message == null) {
            return false;
        }
        beginErrorCondition(parser);
        // An empty block is named at its di, as in the C bosonoga.
        var at = message.startsWith("empty block") ? parser.getContext().getStart() : token;
        parser.notifyErrorListeners(at, message, exception);
        return true;
    }

    // Of a choice that failed past its first token: an else without if takes a
    // block, and what follows a whole initializer is the next statement.
    private static String pastMessage(ParserRuleContext context, Token start, Token token) {
        var isEnd = Tokens.isEnd(token);
        if (context instanceof BosonogaParser.IfStatementContext && start.getType() == BosonogaParser.ELSE) {
            return "expected di, got " + Tokens.shown(token);
        }
        if (context instanceof BosonogaParser.InitializerContext || context instanceof BosonogaParser.AssignedContext) {
            return isEnd ? "expected do, got end of file" : "expected a statement, got " + Tokens.shown(token);
        }
        return null;
    }

    private static String message(Parser parser, ParserRuleContext context, Token token) {
        var isEnd = Tokens.isEnd(token);
        var got = Tokens.shown(token);
        if (context instanceof BosonogaParser.BlockContext block && block.DI() != null) {
            if (token.getType() == BosonogaParser.DO && block.statement().isEmpty()) {
                return "empty block, write nihil for a block that does nothing";
            }
            return isEnd ? "expected do, got end of file" : "expected a statement, got " + got;
        }
        if (context instanceof BosonogaParser.StatementContext statement && statement.getStart() == token) {
            return isEnd ? "expected do, got end of file" : "expected a statement, got " + got;
        }
        var expected = new IntervalSet(parser.getExpectedTokens());
        expected.remove(BosonogaParser.CUT);
        if (expected.contains(BosonogaParser.STRING)) {
            return "expected a value, got " + got;
        }
        switch (context) {
            case BosonogaParser.DeclareStatementContext declare when expected.contains(BosonogaParser.IS) -> {
                return isName(token)
                        ? ("a %s declares one variable, and the values of a call are taken with call ... with "
                        + "ret, one per line").formatted(declare.declaration.getText())
                        : "expected is, got " + got;
            }
            case BosonogaParser.SetStatementContext ignored when expected.contains(BosonogaParser.TO) -> {
                return isName(token)
                        ? "a set sets one variable, and the values of a call are taken with call ... with ret, "
                        + "one per line"
                        : "expected to, to_add, to_subtract, to_multiply, to_divide, to_mod or a conversion, got "
                        + got;
            }
            case BosonogaParser.FunctionDefinitionContext function when expected.contains(BosonogaParser.RET) -> {
                return "expected a parameter whose name ends with its type, like count_i32, got " + got;
            }
            case BosonogaParser.FunctionDefinitionContext ignored when expected.contains(BosonogaParser.NUMBER) -> {
                return "expected a type with its index, like 0_i32, of %s, got %s".formatted(TYPES, got);
            }
            case BosonogaParser.ArrayInitializerContext ignored when expected.contains(BosonogaParser.NUMBER) -> {
                return "expected a type with its index, like 0_i32, of %s, got %s".formatted(TYPES, got);
            }
            case BosonogaParser.TakingContext ignored when expected.contains(BosonogaParser.NUMBER) -> {
                return "expected another value to take, like 1_i32 as count, or di, got " + got;
            }
            case BosonogaParser.RowsContext ignored when expected.contains(BosonogaParser.ROW) -> {
                return "expected row, got " + got;
            }
            case BosonogaParser.CountStatementContext ignored when expected.contains(BosonogaParser.DI) -> {
                return "expected di, got " + got;
            }
            case BosonogaParser.ReturnStatementContext ignored -> {
                return "expected exit_success or exit_failure, got " + got;
            }
            default -> {
            }
        }
        if (expected.size() != 1) {
            return null;
        }
        var type = expected.getMinElement();
        if (type == BosonogaParser.NAME) {
            return switch (context) {
                case BosonogaParser.CallStatementContext ignored ->
                        "expected a function name, which ends with _fn, got " + got;
                case BosonogaParser.FunctionDefinitionContext ignored -> "expected a function name, got " + got;
                case BosonogaParser.NewInitializerContext ignored -> "expected a type, as in new str of, got " + got;
                case BosonogaParser.ArrayLengthContext ignored -> "expected the name of an array, got " + got;
                default -> isNewName(context) ? "expected a variable name, got " + got : null;
            };
        }
        var literal = parser.getVocabulary().getLiteralName(type);
        if (literal == null) {
            return null;
        }
        return "expected %s, got %s".formatted(literal.substring(1, literal.length() - 1), got);
    }

    // A word that is not reserved, as the C bosonoga takes the name of a variable.
    private static boolean isName(Token token) {
        var text = CutToken.original(token).getText();
        return CutToken.original(token).getType() == BosonogaParser.NAME && !text.startsWith("gfx_")
                && !text.equals("expect");
    }

    // Where the C bosonoga takes the name of something new.
    private static boolean isNewName(ParserRuleContext context) {
        return context instanceof BosonogaParser.DeclareStatementContext
                || context instanceof BosonogaParser.BindingContext
                || context instanceof BosonogaParser.CountStatementContext
                || context instanceof BosonogaParser.ConstantDeclarationContext
                || context instanceof BosonogaParser.TopValDeclarationContext
                || context instanceof BosonogaParser.SetStatementContext
                || context instanceof BosonogaParser.UnsetStatementContext;
    }

}
