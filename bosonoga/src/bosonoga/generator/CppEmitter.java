package bosonoga.generator;

import static java.util.stream.Collectors.joining;

import java.math.BigInteger;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.regex.Pattern;
import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.TokenStream;
import org.antlr.v4.runtime.tree.TerminalNode;

final class CppEmitter extends BosonogaBaseVisitor<String> {
    private static final Pattern NUMBER =
            Pattern.compile("(-?[0-9]+)(?:d([0-9]+))?(?:_([A-Za-z0-9_]*))?");
    private static final String SHIFT_PREFIX = "shift_";
    private static final int SHIFT_BIT_MAX = 31;
    private static final String LOOP_PREFIX = "to_";
    private static final String STRING_SUFFIX = "_str";
    private static final String XML_STRING_SUFFIX = "_xml_str";
    private static final String FUNCTION_SUFFIX = "_fn";
    private static final int FUNCTION_VALUES_MAX = 16;
    // A type with its index, as in 0_i32, which names a value of a function.
    private static final Pattern INDEXED = Pattern.compile("(0|[1-9][0-9]{0,8})_([A-Za-z0-9_]+)");
    // The types of the C bosonoga that are for later slices.
    private static final List<String> LATER_KINDS =
            List.of("color", "input_key", "font", "icon", "xml_str", "dir");
    private static final String INDENT = "  ";

    private record Expression(String cpp, Kind kind) {
    }

    private static final class Variable {
        private final Token declaration;
        private final Kind kind;
        private final boolean isVal;
        private boolean isRead;

        private Variable(Token declaration, Kind kind, boolean isVal) {
            this.declaration = declaration;
            this.kind = kind;
            this.isVal = isVal;
        }
    }

    // A constant is a literal, which is written where the constant is read.
    private static final class Constant {
        private final Token declaration;
        private final Expression value;
        private boolean isUsed;

        private Constant(Token declaration, Expression value) {
            this.declaration = declaration;
            this.value = value;
        }
    }

    private record Function(Token name, List<Kind> parameters, List<Kind> results,
                            boolean[] isCalled) {
    }

    private record Indexed(int index, Kind kind) {
    }

    // A variable that a block starts with, as the variable of a loop.
    private record Declared(Token name, Kind kind, String cpp) {
    }

    private final String sourceName;
    private final String functionName;
    private final TokenStream tokens;
    private final Map<String, Constant> constants = new LinkedHashMap<>();
    private final Map<String, Function> functions = new LinkedHashMap<>();
    private Function currentFunction;
    private int resultCount;
    private final Deque<Map<String, Variable>> scopes = new ArrayDeque<>();
    private int depth;
    private int loopDepth;
    private int loopCount;
    private boolean usesSource;

    CppEmitter(String sourceName, String functionName, TokenStream tokens) {
        this.sourceName = sourceName;
        this.functionName = functionName;
        this.tokens = tokens;
    }

    // As in the C bosonoga, a constant is seen only after it is declared.
    @Override
    public String visitProgram(BosonogaParser.ProgramContext context) {
        BosonogaParser.BlockContext main = null;
        String body = null;
        var definitions = new StringBuilder();
        for (var item : context.children) {
            if (item instanceof BosonogaParser.ConstantDeclarationContext constant) {
                declareConstant(constant);
            } else if (item instanceof BosonogaParser.FunctionDefinitionContext function) {
                definitions.append(defineFunction(function)).append('\n');
            } else if (item instanceof BosonogaParser.MainSectionContext section) {
                if (main != null) {
                    throw error(section.BOSONOGA_MAIN().getSymbol(),
                            "expected const, def, val or end of file, got 'bosonoga_main'");
                }
                main = section.block();
                body = visit(main);
                // As in the C bosonoga, reaching the end of bosonoga_main fails when
                // it runs, at what follows its block.
                if (!(main.statement().getLast() instanceof BosonogaParser.ReturnStatementContext)) {
                    body += INDENT + "runtime::fail(" + where(next(main.getStop()))
                            + ", \"the program ended without a return\");\n";
                }
            }
        }
        if (main == null) {
            throw error(context.EOF().getSymbol(), "a program needs a bosonoga_main block");
        }
        for (var constant : constants.values()) {
            if (!constant.isUsed) {
                throw error(constant.declaration,
                        "'%s' is never used".formatted(constant.declaration.getText()));
            }
        }
        for (var function : functions.values()) {
            if (!function.isCalled()[0]) {
                throw error(function.name(), "'%s' is never called".formatted(function.name().getText()));
            }
        }
        var cpp = new StringBuilder("""
                // Generated from %s by the bosonoga generator. Do not edit.
                #include "bosonoga_runtime.hh"

                namespace {

                namespace runtime = bosonoga::runtime;

                """.formatted(sourceName));
        if (usesSource) {
            cpp.append("constexpr const char *source = \"").append(cppStringText(sourceName))
                    .append("\";\n\n");
        }
        cpp.append(definitions).append("int bosonoga_main() {\n").append(body);
        return cpp.append("""
                }

                }  // namespace

                namespace bosonoga::program {

                int %s() {
                  try {
                    return ::bosonoga_main();
                  } catch (const ::bosonoga::runtime::Error &error) {
                    std::cerr << error.what() << '\\n';
                    return EXIT_FAILURE;
                  }
                }

                }  // namespace bosonoga::program
                """.formatted(functionName)).toString();
    }

    private void declareConstant(BosonogaParser.ConstantDeclarationContext context) {
        var name = context.NAME().getSymbol();
        checkNewName(name);
        constants.put(name.getText(), new Constant(name, expression(context.value())));
    }

    @Override
    public String visitBlock(BosonogaParser.BlockContext context) {
        return block(context, List.of(), List.of());
    }

    // A block can start with lines of its own, and with the variables that a loop
    // or a call declares for it.
    private String block(BosonogaParser.BlockContext context, List<String> prelude,
                         List<Declared> declared) {
        scopes.push(new LinkedHashMap<>());
        depth++;
        var lines = new StringBuilder();
        for (var line : prelude) {
            lines.append(INDENT.repeat(depth)).append(line).append('\n');
        }
        for (var variable : declared) {
            declare(variable.name(), variable.kind(), false);
            lines.append(INDENT.repeat(depth)).append(variable.cpp()).append('\n');
        }
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

    @Override
    public String visitInfoStatement(BosonogaParser.InfoStatementContext context) {
        return "runtime::info(std::cout, "
                + context.value().stream().map(value -> expression(value).cpp()).collect(joining(", "))
                + ");";
    }

    @Override
    public String visitDeclareStatement(BosonogaParser.DeclareStatementContext context) {
        var isVal = context.declaration.getType() == BosonogaParser.VAL;
        var initial = switch (context.initializer()) {
            case BosonogaParser.OperationInitializerContext operation -> operation(operation.operation());
            case BosonogaParser.CallInitializerContext call -> takenResult(context.NAME().getSymbol(),
                    call.callee, call.OF(), call.value(), call.result);
            case BosonogaParser.ValueInitializerContext value -> flags(value);
            default -> throw new IllegalStateException("no C++ for " + context.getText());
        };
        var name = context.NAME().getSymbol();
        declare(name, initial.kind(), isVal);
        return (isVal ? "const " : "") + initial.kind().cpp + " " + cppName(name.getText()) + " = "
                + initial.cpp() + ";";
    }

    // A value, which can be flags joined to more flags with shift_or.
    private Expression flags(BosonogaParser.ValueInitializerContext context) {
        var values = context.value();
        var first = expression(values.getFirst());
        if (values.size() > 1 && first.kind() != Kind.FLAGS) {
            throw error(values.getFirst().getStart(), "shift_or needs flags, but '%s' is %s"
                    .formatted(values.getFirst().getText(), first.kind().shown));
        }
        var cpp = new StringBuilder(first.cpp());
        for (var value : values.subList(1, values.size())) {
            cpp.append(" | ").append(expect(value, Kind.FLAGS, "shift_or").cpp());
        }
        return new Expression(cpp.toString(), first.kind());
    }

    @Override
    public String visitSetStatement(BosonogaParser.SetStatementContext context) {
        var name = context.NAME().getSymbol();
        var shown = name.getText();
        if (constants.containsKey(shown)) {
            throw error(name, "'%s' is a constant and cannot be set".formatted(shown));
        }
        var target = find(name).orElseThrow(() -> unknownVariable(name));
        if (target.isVal) {
            throw error(name, "'%s' is a val and cannot be set".formatted(shown));
        }
        var assigned = switch (context.setter()) {
            case BosonogaParser.CallSetterContext call -> {
                var result = takenResult(name, call.callee, call.OF(), call.value(), call.result);
                if (result.kind() != target.kind) {
                    throw error(name, "'%s' is %s, but '%s' returns %s".formatted(shown,
                            target.kind.shown, call.callee.getText(), result.kind().shown));
                }
                yield result.cpp();
            }
            case BosonogaParser.ToSetterContext to -> {
                var values = to.value();
                if (values.size() > 1) {
                    throw error(values.get(1).getStart(),
                            "set ... to sets '%s' to a single value".formatted(shown));
                }
                yield expect(values.getFirst(), target.kind, "set ... to").cpp();
            }
            case BosonogaParser.AccumulationSetterContext accumulation ->
                    accumulation(name, target.kind, accumulation);
            case BosonogaParser.OperationSetterContext operation -> {
                var result = operation(operation.operation());
                if (result.kind() != target.kind) {
                    throw error(operation.operation().function, "'%s' is %s, but the conversion gives %s"
                            .formatted(shown, target.kind.shown, result.kind().shown));
                }
                yield result.cpp();
            }
            default -> throw new IllegalStateException("no C++ for " + context.getText());
        };
        return cppName(shown) + " = " + assigned + ";";
    }

    private String accumulation(Token name, Kind kind,
                                BosonogaParser.AccumulationSetterContext context) {
        var word = context.accumulation.getText();
        if (!kind.isNumber()) {
            throw error(context.accumulation,
                    "'%s' is %s, which cannot be used in arithmetic, but set ... to sets it"
                            .formatted(name.getText(), kind.shown));
        }
        var values = context.value();
        if (context.accumulation.getType() == BosonogaParser.TO_MOD && values.size() < 2) {
            throw error(context.accumulation, "to_mod needs a value to divide by");
        }
        var cpp = values.stream().map(value -> expect(value, kind, word).cpp()).collect(joining(", "));
        var step = word.substring("to_".length());
        return "runtime::accumulate<" + kind.cpp + ">(" + where(name) + ", runtime::Step::" + step
                + ", {" + cpp + "})";
    }

    private Expression operation(BosonogaParser.OperationContext context) {
        var operation = Operation.of(context.function.getText());
        var values = context.value();
        if (values.size() != (operation.isBinary ? 2 : 1)) {
            throw error(context.function, "%s takes %s"
                    .formatted(operation.word, operation.isBinary ? "two values" : "one value"));
        }
        var source = expression(values.getFirst());
        if (!source.kind().isReal() && !(operation.acceptsInteger && source.kind().isInteger())) {
            throw error(values.getFirst().getStart(), "%s needs %s, got %s".formatted(operation.word,
                    operation.acceptsInteger ? "a number" : "f32 or f64", source.kind().shown));
        }
        var result = operation.result(source.kind());
        if (operation.convertsTo != null) {
            return new Expression("runtime::convert<" + result.cpp + ">(" + where(context.function) + ", "
                    + source.cpp() + ")", result);
        }
        var arguments = source.cpp();
        if (operation.isBinary) {
            arguments += ", " + expect(values.get(1), source.kind(), operation.word).cpp();
        }
        return new Expression("runtime::math<" + source.kind().cpp + ">(" + where(context.function)
                + ", runtime::Math::" + operation.math + ", " + arguments + ")", result);
    }

    @Override
    public String visitIfStatement(BosonogaParser.IfStatementContext context) {
        var conditions = context.condition();
        var blocks = context.block();
        var indent = INDENT.repeat(depth);
        var cpp = new StringBuilder();
        for (int i = 0; i < conditions.size(); i++) {
            cpp.append(i == 0 ? "if (" : " else if (").append(visit(conditions.get(i))).append(") {\n")
                    .append(visit(blocks.get(i))).append(indent).append('}');
        }
        if (blocks.size() > conditions.size()) {
            cpp.append(" else {\n").append(visit(blocks.getLast())).append(indent).append('}');
        }
        return cpp.toString();
    }

    @Override
    public String visitWhileStatement(BosonogaParser.WhileStatementContext context) {
        var condition = visit(context.condition());
        loopDepth++;
        var block = visit(context.block());
        loopDepth--;
        return "while (" + condition + ") {\n" + block + INDENT.repeat(depth) + "}";
    }

    // As in the C bosonoga, the variable is a copy of a hidden counter, so setting
    // it does not change the iterations.
    @Override
    public String visitCountStatement(BosonogaParser.CountStatementContext context) {
        var loop = context.LOOP_COUNT().getSymbol();
        var text = loop.getText();
        long limit;
        try {
            limit = Long.parseLong(text.substring(LOOP_PREFIX.length()));
        } catch (NumberFormatException exception) {
            limit = Long.MAX_VALUE;
        }
        if (limit > Integer.MAX_VALUE) {
            throw error(loop, "'%s' does not fit in 32 bits".formatted(text));
        }
        var counter = "loop_" + ++loopCount;
        var name = context.NAME() == null ? null : context.NAME().getSymbol();
        loopDepth++;
        var block = block(context.block(), List.of(), name == null
                ? List.of()
                : List.of(new Declared(name, Kind.I32,
                        "std::int32_t " + cppName(name.getText()) + " = " + counter + ";")));
        loopDepth--;
        return "for (std::int32_t " + counter + " = 0; " + counter + " < " + limit + "; ++" + counter
                + ") {\n" + block + INDENT.repeat(depth) + "}";
    }

    @Override
    public String visitBreakStatement(BosonogaParser.BreakStatementContext context) {
        return loopJump(context.BREAK().getSymbol());
    }

    @Override
    public String visitContinueStatement(BosonogaParser.ContinueStatementContext context) {
        return loopJump(context.CONTINUE().getSymbol());
    }

    private String loopJump(Token jump) {
        if (loopDepth == 0) {
            throw error(jump, "%s can only be in a loop".formatted(jump.getText()));
        }
        return jump.getText() + ";";
    }

    @Override
    public String visitSleepStatement(BosonogaParser.SleepStatementContext context) {
        return "runtime::sleep_ms(" + expect(context.value(), Kind.U32, "sleep_ms").cpp() + ");";
    }

    @Override
    public String visitNihilStatement(BosonogaParser.NihilStatementContext context) {
        return "// nihil";
    }

    @Override
    public String visitReturnStatement(BosonogaParser.ReturnStatementContext context) {
        var start = context.RETURN().getSymbol();
        var values = context.value();
        if (currentFunction == null) {
            if (context.status == null) {
                var got = values.isEmpty() ? next(start) : values.getFirst().getStart();
                throw error(got, "expected exit_success or exit_failure, got %s".formatted(shown(got)));
            }
            return context.status.getType() == BosonogaParser.EXIT_SUCCESS
                    ? "return EXIT_SUCCESS;"
                    : "return EXIT_FAILURE;";
        }
        if (context.status != null) {
            throw error(context.status, "only bosonoga_main and gfx_main return an exit status, "
                    + "a function returns its values");
        }
        var name = currentFunction.name().getText();
        var results = currentFunction.results();
        if (values.size() < results.size()) {
            throw error(start, "'%s' returns %s, but this return has %d"
                    .formatted(name, valuesShown(results), values.size()));
        }
        if (values.size() > results.size()) {
            throw error(start, "'%s' returns %s, but this return has more"
                    .formatted(name, valuesShown(results)));
        }
        var cpp = new ArrayList<String>();
        for (int i = 0; i < results.size(); i++) {
            cpp.add(expect(values.get(i), results.get(i), "return").cpp());
        }
        return switch (cpp.size()) {
            case 0 -> "return;";
            case 1 -> "return " + cpp.getFirst() + ";";
            default -> "return {" + String.join(", ", cpp) + "};";
        };
    }

    // As in the C bosonoga, a function is known before its block, so that it can
    // call itself, and it sees the constants but no variables of other blocks.
    private String defineFunction(BosonogaParser.FunctionDefinitionContext context) {
        var name = context.name;
        var text = name.getText();
        if (!isFunctionName(text)) {
            if (text.equals(FUNCTION_SUFFIX)) {
                throw error(name, "the name of a function has more before _fn, as in double_fn");
            }
            // The old fn_ prefix, without it in the suggestion.
            var suggested = text.startsWith("fn_") && text.length() > 3 ? text.substring(3) : text;
            throw error(name, "the name of a function ends with _fn, as in %s_fn".formatted(suggested));
        }
        if (functions.containsKey(text)) {
            throw error(name, "function '%s' already exists".formatted(text));
        }
        if (constants.containsKey(text)) {
            throw error(name, "'%s' is already a constant".formatted(text));
        }
        scopes.push(new LinkedHashMap<>());
        var parameters = new ArrayList<Kind>();
        var cppParameters = new StringBuilder("const runtime::Where &called_at");
        for (var parameter : context.parameters) {
            var kind = parameterKind(parameter);
            if (parameters.size() == FUNCTION_VALUES_MAX) {
                throw error(parameter, "a function takes at most %d values".formatted(FUNCTION_VALUES_MAX));
            }
            declare(parameter, kind, false);
            parameters.add(kind);
            cppParameters.append(", ").append(kind.cpp).append(' ').append(cppName(parameter.getText()));
        }
        var results = new ArrayList<Kind>();
        if (context.RET() != null) {
            for (var result : context.results) {
                var indexed = indexed(result).orElseThrow(() -> error(result, ("expected a type with its "
                        + "index, like 0_i32, of i32, i64, u32, u64, f32, f64, flags, color, str or bool, "
                        + "got '%s'").formatted(result.getText())));
                if (results.size() == FUNCTION_VALUES_MAX) {
                    throw error(result, "a function returns at most %d values".formatted(FUNCTION_VALUES_MAX));
                }
                if (indexed.index() != results.size()) {
                    throw error(result, "the values are numbered from 0 in order, so '%s' is %d_%s"
                            .formatted(result.getText(), results.size(), indexed.kind().shown));
                }
                results.add(indexed.kind());
            }
        } else if (context.RET_VOID() == null) {
            throw error(context.block().getStart(), ("'%s' must say what it returns: ret and its types, "
                    + "or ret_void for nothing").formatted(text));
        }
        var function = new Function(name, List.copyOf(parameters), List.copyOf(results), new boolean[1]);
        functions.put(text, function);
        currentFunction = function;
        var body = block(context.block(), List.of("const runtime::Call call(called_at);"), List.of());
        if (!results.isEmpty()
                && !(context.block().statement().getLast() instanceof BosonogaParser.ReturnStatementContext)) {
            body += INDENT + "runtime::fail(" + where(name) + ", \"the function '" + text
                    + "' ended without a return\");\n";
        }
        currentFunction = null;
        for (var parameter : scopes.pop().values()) {
            if (!parameter.isRead) {
                throw error(parameter.declaration,
                        "'%s' is never read".formatted(parameter.declaration.getText()));
            }
        }
        var returned = switch (results.size()) {
            case 0 -> "void";
            case 1 -> results.getFirst().cpp;
            default -> "std::tuple<" + results.stream().map(kind -> kind.cpp).collect(joining(", ")) + ">";
        };
        return returned + " " + cppFunctionName(text) + "(" + cppParameters + ") {\n" + body + "}\n";
    }

    // The type is after an underscore, as in count_i32, and the longest type is
    // taken, as in the C bosonoga.
    private Kind parameterKind(Token parameter) {
        var text = parameter.getText();
        for (int start = 2; start < text.length(); start++) {
            if (text.charAt(start - 1) != '_') {
                continue;
            }
            var suffix = text.substring(start);
            if (LATER_KINDS.contains(suffix)) {
                throw error(parameter, "a parameter of %s is not supported yet".formatted(suffix));
            }
            var kind = Kind.named(suffix);
            if (kind.isPresent()) {
                return kind.get();
            }
        }
        throw error(parameter, "expected a parameter whose name ends with its type, like count_i32, got '%s'"
                .formatted(text));
    }

    private Optional<Indexed> indexed(Token token) {
        var matcher = INDEXED.matcher(token.getText());
        if (!matcher.matches()) {
            return Optional.empty();
        }
        if (LATER_KINDS.contains(matcher.group(2))) {
            throw error(token, "a value of %s is not supported yet".formatted(matcher.group(2)));
        }
        return Kind.named(matcher.group(2)).map(kind -> new Indexed(Integer.parseInt(matcher.group(1)), kind));
    }

    @Override
    public String visitCallStatement(BosonogaParser.CallStatementContext context) {
        var function = callable(context.callee);
        var name = context.callee.getText();
        var call = call(function, context.callee, context.OF(), context.value());
        var results = function.results();
        if (results.isEmpty()) {
            if (context.WITH() != null) {
                throw error(context.WITH().getSymbol(),
                        "'%s' returns no values, so its call has neither with ret nor a block".formatted(name));
            }
            return call + ";";
        }
        if (context.WITH() == null) {
            throw needsBlock(context.CALL().getSymbol(), function);
        }
        // Copied out first, as in the C bosonoga.
        var result = "result_" + ++resultCount;
        var declared = new ArrayList<Declared>();
        Indexed previous = null;
        for (var binding : context.binding()) {
            var position = binding.NUMBER().getSymbol();
            var shownPosition = position.getText();
            var taken = indexed(position)
                    .filter(indexed -> indexed.index() < results.size()
                            && indexed.kind() == results.get(indexed.index()))
                    .orElseThrow(() -> error(position, "'%s' returns %s, so it has no %s"
                            .formatted(name, kindsNamed(results), shownPosition)));
            if (previous != null && taken.index() <= previous.index()) {
                throw error(position, ("the values are taken once each, by increasing index, so %s cannot "
                        + "follow %d_%s").formatted(shownPosition, previous.index(), previous.kind().shown));
            }
            previous = taken;
            var value = results.size() == 1 ? result : "std::get<" + taken.index() + ">(" + result + ")";
            declared.add(new Declared(binding.NAME().getSymbol(), taken.kind(),
                    taken.kind().cpp + " " + cppName(binding.NAME().getText()) + " = " + value + ";"));
        }
        var block = block(context.block(), List.of("const auto " + result + " = " + call + ";"), declared);
        return "{\n" + block + INDENT.repeat(depth) + "}";
    }

    // The one value of a call that var and set ... to take, as with ret 0_i32.
    private Expression takenResult(Token target, Token callee, TerminalNode of,
                                   List<BosonogaParser.ValueContext> values, Token result) {
        var function = callable(callee);
        var name = callee.getText();
        var call = call(function, callee, of, values);
        var results = function.results();
        if (results.isEmpty()) {
            throw error(target, "'%s' returns no values, so it is called with call".formatted(name));
        }
        if (results.size() > 1) {
            throw needsBlock(target, function);
        }
        var kind = results.getFirst();
        var taken = indexed(result);
        if (taken.isEmpty() || taken.get().index() != 0 || taken.get().kind() != kind) {
            throw error(result, "'%s' returns %s, so its call ends with ret 0_%s, not '%s'"
                    .formatted(name, kind.shown, kind.shown, result.getText()));
        }
        return new Expression(call, kind);
    }

    private BosonogaException needsBlock(Token token, Function function) {
        var results = function.results();
        return error(token, ("'%s' returns %s, which %scall ... with ret takes for its block, each named, "
                + "as in with ret 0_%s as value").formatted(function.name().getText(), kindsNamed(results),
                results.size() == 1 ? "var, set ... to or " : "", results.getFirst().shown));
    }

    // A function that only calls itself is not used, as in the C bosonoga.
    private Function callable(Token callee) {
        var function = functions.get(callee.getText());
        if (function == null) {
            throw error(callee, "unknown function '%s'".formatted(callee.getText()));
        }
        if (function != currentFunction) {
            function.isCalled()[0] = true;
        }
        return function;
    }

    private String call(Function function, Token callee, TerminalNode of,
                        List<BosonogaParser.ValueContext> values) {
        var name = callee.getText();
        var parameters = function.parameters();
        var shownParameters = valuesShown(parameters);
        if (parameters.isEmpty() && of != null) {
            throw error(of.getSymbol(), "'%s' takes no values, so its call has no of".formatted(name));
        }
        if (!parameters.isEmpty() && of == null) {
            throw error(next(callee), "'%s' takes %s, which follow of, as in %s of ..."
                    .formatted(name, shownParameters, name));
        }
        var arguments = new StringBuilder(where(callee));
        for (int i = 0; i < parameters.size(); i++) {
            if (i == values.size()) {
                throw error(next(values.getLast().getStop()), "'%s' takes %s, but is given %d"
                        .formatted(name, shownParameters, i));
            }
            arguments.append(", ").append(expect(values.get(i), parameters.get(i), name).cpp());
        }
        if (values.size() > parameters.size()) {
            throw error(values.get(parameters.size()).getStart(), "'%s' takes %s, but is given more"
                    .formatted(name, shownParameters));
        }
        return cppFunctionName(name) + "(" + arguments + ")";
    }

    private static String valuesShown(List<Kind> kinds) {
        if (kinds.isEmpty()) {
            return "no values";
        }
        return kinds.size() + (kinds.size() == 1 ? " value," : " values,")
                + kinds.stream().map(kind -> " " + kind.shown).collect(joining());
    }

    private static String kindsNamed(List<Kind> kinds) {
        var named = new ArrayList<String>();
        for (int i = 0; i < kinds.size(); i++) {
            named.add(i + "_" + kinds.get(i).shown);
        }
        return String.join(", ", named);
    }

    private static boolean isFunctionName(String name) {
        return name.length() > FUNCTION_SUFFIX.length() && name.endsWith(FUNCTION_SUFFIX);
    }

    private static String cppFunctionName(String name) {
        return "f_" + name;
    }

    // A comparison is in parentheses only when it is joined to others, as clang
    // warns of if ((a == b)), and each group of and is too, as and binds tighter.
    @Override
    public String visitCondition(BosonogaParser.ConditionContext context) {
        return joined(context.conjunction().stream().map(this::visit).toList(), " || ");
    }

    @Override
    public String visitConjunction(BosonogaParser.ConjunctionContext context) {
        return joined(context.comparison().stream().map(this::visit).toList(), " && ");
    }

    private static String joined(List<String> parts, String operator) {
        return parts.size() == 1
                ? parts.getFirst()
                : parts.stream().map(part -> "(" + part + ")").collect(joining(operator));
    }

    @Override
    public String visitComparison(BosonogaParser.ComparisonContext context) {
        var left = expression(context.value(0));
        String cpp;
        if (context.compare != null) {
            cpp = comparison(context.compare, left, expression(context.value(1)));
        } else if (left.kind() == Kind.BOOL) {
            cpp = left.cpp();
        } else {
            throw error(next(context.getStop()), "expected a comparison, got %s"
                    .formatted(shown(next(context.getStop()))));
        }
        return context.NOT() != null ? "!(" + cpp + ")" : cpp;
    }

    private Token next(Token token) {
        return tokens.get(token.getTokenIndex() + 1);
    }

    private static String shown(Token token) {
        return token.getType() == Token.EOF ? "end of file" : "'" + token.getText() + "'";
    }

    private String comparison(Token compare, Expression left, Expression right) {
        var word = compare.getText();
        if (compare.getType() == BosonogaParser.SHIFT_HAS) {
            if (left.kind() != Kind.FLAGS || right.kind() != Kind.FLAGS) {
                throw error(compare, "shift_has needs flags, not %s and %s"
                        .formatted(left.kind().shown, right.kind().shown));
            }
            // As in the C bosonoga, the left flags have every bit of the right flags.
            return "(" + left.cpp() + " & " + right.cpp() + ") == " + right.cpp();
        }
        var kind = left.kind();
        if (kind != right.kind()) {
            throw error(compare, "cannot compare %s with %s".formatted(kind.shown, right.kind().shown));
        }
        if (kind == Kind.FLAGS) {
            throw error(compare, "flags can only be compared with shift_has");
        }
        if ((kind == Kind.BOOL || kind == Kind.STR)
                && compare.getType() != BosonogaParser.EQ && compare.getType() != BosonogaParser.NOT_EQ) {
            throw error(compare, "%s can only be compared with eq or not_eq".formatted(kind.shown));
        }
        var operator = switch (compare.getType()) {
            case BosonogaParser.GT -> ">";
            case BosonogaParser.LT -> "<";
            case BosonogaParser.GE -> ">=";
            case BosonogaParser.LE -> "<=";
            case BosonogaParser.EQ -> "==";
            case BosonogaParser.NOT_EQ -> "!=";
            default -> null;
        };
        if (operator != null) {
            if (kind.isReal() && (operator.equals("==") || operator.equals("!="))) {
                throw error(compare, "%s has no exact %s, use %s_3 or %s_6"
                        .formatted(kind.shown, word, word, word));
            }
            return left.cpp() + " " + operator + " " + right.cpp();
        }
        if (kind.isInteger()) {
            throw error(compare, "%s only works on f32 or f64, %s uses eq or not_eq"
                    .formatted(word, kind.shown));
        }
        var tolerance = word.endsWith("_3") ? "1e-3" : "1e-6";
        var close = "runtime::close(" + left.cpp() + ", " + right.cpp() + ", " + tolerance + ")";
        return word.startsWith("not_") ? "!" + close : close;
    }

    private Expression expression(BosonogaParser.ValueContext value) {
        return switch (value) {
            case BosonogaParser.NumberValueContext number -> number(number.NUMBER().getSymbol());
            case BosonogaParser.ShiftValueContext shift -> new Expression(
                    "std::uint32_t{1U << " + parseShiftBit(shift.SHIFT_BIT().getSymbol()) + "}",
                    Kind.FLAGS);
            case BosonogaParser.BoolValueContext bool ->
                    new Expression(bool.TRUE_BOOL() != null ? "true" : "false", Kind.BOOL);
            case BosonogaParser.StringValueContext string -> string(string.STRING().getSymbol());
            case BosonogaParser.NameValueContext name -> read(name.NAME().getSymbol());
            default -> throw new IllegalStateException("no C++ for the value " + value.getText());
        };
    }

    // As in the C bosonoga, a str has no escape sequences, so it is the text
    // between its quotes, and the C++ escapes only what C++ needs.
    private Expression string(Token literal) {
        var text = literal.getText();
        var end = text.lastIndexOf('"');
        var content = text.substring(1, end);
        var suffix = text.substring(end + 1);
        for (int i = 0; i < content.length(); i++) {
            var character = content.charAt(i);
            if (character == '\\' || character == '\0') {
                throw new BosonogaException(sourceName, literal.getLine(),
                        literal.getCharPositionInLine() + 2 + i,
                        character == '\\' ? "escape sequences are not supported" : "NUL byte in string");
            }
        }
        if (suffix.equals(XML_STRING_SUFFIX)) {
            throw error(literal, "an xml_str is not supported yet, but a str is, as in \"text\"_str");
        }
        if (!suffix.equals(STRING_SUFFIX)) {
            throw error(literal, "a string must have the suffix _str or _xml_str, as in \"text\"_str");
        }
        var cpp = new StringBuilder("std::string{\"");
        content.codePoints().forEach(codePoint -> {
            if (codePoint < 0x20 || codePoint == 0x7f) {
                cpp.append("\\%03o".formatted(codePoint));
            } else {
                cpp.appendCodePoint(codePoint);
            }
        });
        return new Expression(cpp.append("\"}").toString(), Kind.STR);
    }

    private Expression expect(BosonogaParser.ValueContext value, Kind kind, String context) {
        var expression = expression(value);
        if (expression.kind() != kind) {
            throw error(value.getStart(), "%s needs %s, but '%s' is %s"
                    .formatted(context, kind.shown, value.getText(), expression.kind().shown));
        }
        return expression;
    }

    private Expression number(Token literal) {
        var text = literal.getText();
        var matcher = NUMBER.matcher(text);
        if (!matcher.matches()) {
            throw new IllegalStateException("not a number: " + text);
        }
        var kind = Optional.ofNullable(matcher.group(3)).flatMap(Kind::number)
                .orElseThrow(() -> error(literal,
                        "number must have the suffix _i32, _i64, _u32, _u64, _f32 or _f64"));
        var whole = matcher.group(1);
        var fraction = matcher.group(2);
        var range = "the number is outside the %s range".formatted(kind.shown);
        if (kind.isInteger()) {
            if (fraction != null) {
                throw error(literal, "an integer cannot have a fraction");
            }
            var value = new BigInteger(whole);
            if (!fits(kind, value)) {
                throw error(literal, range);
            }
            return new Expression(wholeLiteral(kind, value), kind);
        }
        var real = Double.parseDouble(whole + "." + (fraction == null ? "0" : fraction));
        // Written so that NaN is outside the range too, as in the C bosonoga.
        if (kind == Kind.F32 ? !(Math.abs(real) <= Float.MAX_VALUE) : !Double.isFinite(real)) {
            throw error(literal, range);
        }
        // A hexadecimal literal is exact, where a decimal one would be rounded again.
        return new Expression(kind == Kind.F32
                ? Float.toHexString((float) real) + "F"
                : Double.toHexString(real), kind);
    }

    private static boolean fits(Kind kind, BigInteger value) {
        var bits = kind == Kind.I32 || kind == Kind.U32 ? 32 : 64;
        var isSigned = kind == Kind.I32 || kind == Kind.I64;
        var min = isSigned ? BigInteger.ONE.shiftLeft(bits - 1).negate() : BigInteger.ZERO;
        var max = isSigned
                ? BigInteger.ONE.shiftLeft(bits - 1).subtract(BigInteger.ONE)
                : BigInteger.ONE.shiftLeft(bits).subtract(BigInteger.ONE);
        return value.compareTo(min) >= 0 && value.compareTo(max) <= 0;
    }

    private static String wholeLiteral(Kind kind, BigInteger value) {
        return switch (kind) {
            case I32 -> "std::int32_t{" + value + "}";
            // The literal 9223372036854775808 does not fit in a signed type.
            case I64 -> value.equals(BigInteger.valueOf(Long.MIN_VALUE))
                    ? "std::int64_t{-9223372036854775807LL - 1}"
                    : "std::int64_t{" + value + "LL}";
            case U32 -> "std::uint32_t{" + value + "U}";
            case U64 -> "std::uint64_t{" + value + "ULL}";
            default -> throw new IllegalStateException(kind + " is not an integer");
        };
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

    private void declare(Token name, Kind kind, boolean isVal) {
        checkNewName(name);
        scopes.getFirst().put(name.getText(), new Variable(name, kind, isVal));
    }

    private void checkNewName(Token name) {
        if (constants.containsKey(name.getText())) {
            throw error(name, "a constant with this name already exists");
        }
        if (find(name).isPresent()) {
            throw error(name, "variable '%s' already exists".formatted(name.getText()));
        }
        if (isFunctionName(name.getText())) {
            throw error(name, "'%s' ends with _fn, which only the name of a function does"
                    .formatted(name.getText()));
        }
        if (functions.containsKey(name.getText())) {
            throw error(name, "'%s' is already a function".formatted(name.getText()));
        }
    }

    private Optional<Variable> find(Token name) {
        for (var scope : scopes) {
            var variable = scope.get(name.getText());
            if (variable != null) {
                return Optional.of(variable);
            }
        }
        return Optional.empty();
    }

    private Expression read(Token name) {
        var constant = constants.get(name.getText());
        if (constant != null) {
            constant.isUsed = true;
            return constant.value;
        }
        if (functions.containsKey(name.getText())) {
            throw error(name, "'%s' is a function, which is called with var, set ... to or call"
                    .formatted(name.getText()));
        }
        var variable = find(name).orElseThrow(() -> unknownVariable(name));
        variable.isRead = true;
        return new Expression(cppName(name.getText()), variable.kind);
    }

    private BosonogaException unknownVariable(Token name) {
        return error(name, "unknown variable '%s'".formatted(name.getText()));
    }

    // Where a runtime error is reported, as in the C bosonoga.
    private String where(Token token) {
        usesSource = true;
        return "{source, " + token.getLine() + ", " + (token.getCharPositionInLine() + 1) + "}";
    }

    // The prefix keeps a bosonoga name from being a C++ keyword, such as new.
    private static String cppName(String name) {
        return "v_" + name;
    }

    private static String cppStringText(String text) {
        return text.replace("\\", "\\\\").replace("\"", "\\\"");
    }

    private BosonogaException error(Token token, String message) {
        return new BosonogaException(sourceName, token.getLine(), token.getCharPositionInLine() + 1,
                message);
    }
}
