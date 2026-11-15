package bosonoga.generator;

import static java.util.stream.Collectors.joining;

import java.util.ArrayDeque;
import java.util.Deque;
import java.util.LinkedHashMap;
import java.util.Map;
import org.antlr.v4.runtime.Token;

final class CppEmitter extends BosonogaBaseVisitor<String> {
    private static final String I32_SUFFIX = "_i32";
    private static final String SHIFT_PREFIX = "shift_";
    private static final int SHIFT_BIT_MAX = 31;
    private static final String INDENT = "  ";

    private enum Kind {
        I32("i32", "std::int32_t"),
        FLAGS("flags", "std::uint32_t");

        private final String shown;
        private final String cpp;

        Kind(String shown, String cpp) {
            this.shown = shown;
            this.cpp = cpp;
        }
    }

    private record Expression(String cpp, Kind kind) {
    }

    private static final class Variable {
        private final Token declaration;
        private final Kind kind;
        private boolean isRead;

        private Variable(Token declaration, Kind kind) {
            this.declaration = declaration;
            this.kind = kind;
        }
    }

    private final String sourceName;
    private final String functionName;
    private final Deque<Map<String, Variable>> scopes = new ArrayDeque<>();
    private int depth;

    CppEmitter(String sourceName, String functionName) {
        this.sourceName = sourceName;
        this.functionName = functionName;
    }

    @Override
    public String visitProgram(BosonogaParser.ProgramContext context) {
        var cpp = new StringBuilder("""
                // Generated from %s by the bosonoga generator. Do not edit.
                #include <cstdint>
                #include <cstdlib>
                #include <iostream>

                namespace bosonoga::program {

                int %s() {
                """.formatted(sourceName, functionName));
        cpp.append(visit(context.block()));
        // Like the C bosonoga, reaching the end of bosonoga_main fails when it runs.
        if (!(context.block().statement().getLast() instanceof BosonogaParser.ReturnStatementContext)) {
            cpp.append(INDENT).append("std::cerr << \"").append(cppStringText(sourceName))
                    .append(": bosonoga_main ended without a return\\n\";\n")
                    .append(INDENT).append("return EXIT_FAILURE;\n");
        }
        return cpp.append("""
                }

                }  // namespace bosonoga::program
                """).toString();
    }

    @Override
    public String visitBlock(BosonogaParser.BlockContext context) {
        scopes.push(new LinkedHashMap<>());
        depth++;
        var lines = new StringBuilder();
        for (var statement : context.statement()) {
            lines.append(INDENT.repeat(depth)).append(visit(statement)).append('\n');
        }
        depth--;
        for (var variable : scopes.pop().values()) {
            if (!variable.isRead) {
                throw error(variable.declaration,
                        "'%s' is never read".formatted(variable.declaration.getText()));
            }
        }
        return lines.toString();
    }

    // Like the C bosonoga, info prints its values with nothing between them.
    @Override
    public String visitInfoStatement(BosonogaParser.InfoStatementContext context) {
        return "std::cout << "
                + context.value().stream()
                        .map(value -> expect(value, Kind.I32, "info").cpp())
                        .collect(joining(" << "))
                + " << '\\n';";
    }

    @Override
    public String visitVarStatement(BosonogaParser.VarStatementContext context) {
        var values = context.value();
        var initial = expression(values.getFirst());
        if (values.size() > 1 && initial.kind() != Kind.FLAGS) {
            throw error(context.SHIFT_OR(0).getSymbol(),
                    "shift_or takes flags, not %s".formatted(initial.kind().shown));
        }
        var cpp = new StringBuilder(initial.cpp());
        for (var value : values.subList(1, values.size())) {
            cpp.append(" | ").append(expect(value, Kind.FLAGS, "shift_or").cpp());
        }
        var name = context.NAME().getSymbol();
        declare(name, initial.kind());
        return initial.kind().cpp + " " + cppName(name.getText()) + " = " + cpp + ";";
    }

    @Override
    public String visitIfStatement(BosonogaParser.IfStatementContext context) {
        var condition = visit(context.condition());
        var block = visit(context.block());
        return "if (" + condition + ") {\n" + block + INDENT.repeat(depth) + "}";
    }

    @Override
    public String visitReturnStatement(BosonogaParser.ReturnStatementContext context) {
        return context.status.getType() == BosonogaParser.EXIT_SUCCESS
                ? "return EXIT_SUCCESS;"
                : "return EXIT_FAILURE;";
    }

    @Override
    public String visitCondition(BosonogaParser.ConditionContext context) {
        return context.comparison().stream().map(this::visit).collect(joining(" && "));
    }

    // As in the C bosonoga, the left flags have every bit of the right flags.
    @Override
    public String visitComparison(BosonogaParser.ComparisonContext context) {
        var left = expression(context.value(0));
        var right = expression(context.value(1));
        if (left.kind() != Kind.FLAGS || right.kind() != Kind.FLAGS) {
            throw error(context.SHIFT_HAS().getSymbol(), "shift_has needs flags, not %s and %s"
                    .formatted(left.kind().shown, right.kind().shown));
        }
        return "((" + left.cpp() + " & " + right.cpp() + ") == " + right.cpp() + ")";
    }

    private Expression expression(BosonogaParser.ValueContext value) {
        return switch (value) {
            case BosonogaParser.I32ValueContext i32 -> new Expression(
                    "std::int32_t{" + parseI32(i32.I32_LITERAL().getSymbol()) + "}", Kind.I32);
            case BosonogaParser.ShiftValueContext shift -> new Expression(
                    "std::uint32_t{1U << " + parseShiftBit(shift.SHIFT_BIT().getSymbol()) + "}",
                    Kind.FLAGS);
            case BosonogaParser.NameValueContext name -> read(name.NAME().getSymbol());
            default -> throw new IllegalStateException("no C++ for the value " + value.getText());
        };
    }

    private Expression expect(BosonogaParser.ValueContext value, Kind kind, String taker) {
        var expression = expression(value);
        if (expression.kind() != kind) {
            throw error(value.getStart(),
                    "%s takes %s, not %s".formatted(taker, kind.shown, expression.kind().shown));
        }
        return expression;
    }

    private void declare(Token name, Kind kind) {
        for (var scope : scopes) {
            if (scope.containsKey(name.getText())) {
                throw error(name, "'%s' is already declared".formatted(name.getText()));
            }
        }
        scopes.getFirst().put(name.getText(), new Variable(name, kind));
    }

    private Expression read(Token name) {
        for (var scope : scopes) {
            var variable = scope.get(name.getText());
            if (variable != null) {
                variable.isRead = true;
                return new Expression(cppName(name.getText()), variable.kind);
            }
        }
        throw error(name, "'%s' is not declared".formatted(name.getText()));
    }

    // The prefix keeps a bosonoga name from being a C++ keyword, such as new.
    private static String cppName(String name) {
        return "v_" + name;
    }

    private static String cppStringText(String text) {
        return text.replace("\\", "\\\\").replace("\"", "\\\"");
    }

    private int parseI32(Token literal) {
        var text = literal.getText();
        try {
            return Integer.parseInt(text, 0, text.length() - I32_SUFFIX.length(), 10);
        } catch (NumberFormatException exception) {
            throw error(literal, "'%s' does not fit in i32".formatted(text));
        }
    }

    private int parseShiftBit(Token literal) {
        var text = literal.getText();
        int bit;
        try {
            bit = Integer.parseInt(text, SHIFT_PREFIX.length(), text.length(), 10);
        } catch (NumberFormatException exception) {
            bit = SHIFT_BIT_MAX + 1;
        }
        if (bit > SHIFT_BIT_MAX) {
            throw error(literal, "'%s' is out of range, the last bit is %s%d"
                    .formatted(text, SHIFT_PREFIX, SHIFT_BIT_MAX));
        }
        return bit;
    }

    private BosonogaException error(Token token, String message) {
        return new BosonogaException(sourceName, token.getLine(), token.getCharPositionInLine() + 1,
                message);
    }
}
