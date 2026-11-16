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
import java.util.function.UnaryOperator;
import java.util.regex.Pattern;
import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.TokenStream;

final class CppEmitter extends BosonogaBaseVisitor<String> {
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
    private static final List<String> LATER_KINDS = List.of("input_key", "font", "icon", "xml_str", "dir");
    private static final String COLOR_PREFIX = "color_";
    private static final String GLOBAL_PREFIX = "global_";
    private static final String KEY_PREFIX = "key_";
    private static final String FONT_PREFIX = "font_";
    private static final String ICON_PREFIX = "icon_";
    private static final String DIR_PREFIX = "dir_";
    private static final String INDENT = "  ";

    private static final int ARRAY_VALUES_MAX = 65536;
    private static final String NEW_READS = "is only read by info, a comparison, new, to_join, io_write, "
            + "an xml_ statement or str_parse, and is not copied";

    private static final class Variable {
        private final Token declaration;
        private final Kind kind;
        private final boolean isVal;
        private final boolean isNew;
        private final int loopDepth;
        private final String cpp;
        // The rows of a var array, which is a variable of its block.
        private Function array;
        private boolean isRead;

        private Variable(Token declaration, Kind kind, boolean isVal, boolean isNew, int loopDepth,
                         String cpp) {
            this.declaration = declaration;
            this.kind = kind;
            this.isVal = isVal;
            this.isNew = isNew;
            this.loopDepth = loopDepth;
            this.cpp = cpp;
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

    // A function, or an array, which the C bosonoga reads as a function of its
    // index, so an array has one i32 parameter and its columns as results.
    private record Function(Token name, List<Kind> parameters, List<Kind> results,
                            boolean[] isCalled, String cpp, boolean isArray, int rowCount) {
        String gives() {
            return isArray ? "has rows of" : "returns";
        }
    }

    private record Indexed(int index, Kind kind) {
    }

    // A variable that a block starts with, as the variable of a loop, whose line
    // is made from its C++ name.
    private record Declared(Token name, Kind kind, UnaryOperator<String> line) {
    }

    private final String sourceName;
    private final String functionName;
    private final TokenStream tokens;
    private final Settings settings;
    private final Map<String, Constant> constants = new LinkedHashMap<>();
    private final Map<String, Function> functions = new LinkedHashMap<>();
    private final Map<String, Function> arrays = new LinkedHashMap<>();
    // How often each name is declared, so that a name declared again after unset
    // has a C++ name of its own.
    private final Map<String, Integer> declarations = new LinkedHashMap<>();
    private boolean readsNew;
    private Function currentFunction;
    private int resultCount;
    private final Deque<Map<String, Variable>> scopes = new ArrayDeque<>();
    // The variables at the top of bosonoga_global, which later sections see.
    private Map<String, Variable> globalScope = Map.of();
    private boolean inGlobal;
    private int depth;
    private int loopDepth;
    private int loopCount;
    private boolean usesSource;

    CppEmitter(String sourceName, String functionName, TokenStream tokens, Settings settings) {
        this.sourceName = sourceName;
        this.functionName = functionName;
        this.tokens = tokens;
        this.settings = settings;
    }

    // As in the C bosonoga, a constant is seen only after it is declared.
    @Override
    public String visitProgram(BosonogaParser.ProgramContext context) {
        BosonogaParser.BlockContext main = null;
        BosonogaParser.GlobalSectionContext global = null;
        var body = new StringBuilder();
        var definitions = new StringBuilder();
        for (var item : context.children) {
            if (item instanceof BosonogaParser.ConstantDeclarationContext constant) {
                declareConstant(constant);
            } else if (item instanceof BosonogaParser.FunctionDefinitionContext function) {
                definitions.append(defineFunction(function)).append('\n');
            } else if (item instanceof BosonogaParser.TopValDeclarationContext array) {
                definitions.append(defineArray(array)).append('\n');
            } else if (item instanceof BosonogaParser.GlobalSectionContext section) {
                if (main != null || global != null) {
                    throw error(section.BOSONOGA_GLOBAL().getSymbol(), "bosonoga_global must be the first section");
                }
                global = section;
                // Its statements are before those of bosonoga_main, in the same C++ scope.
                inGlobal = true;
                body.append(visit(section.block()));
                inGlobal = false;
            } else if (item instanceof BosonogaParser.MainSectionContext section) {
                if (main != null) {
                    throw error(section.BOSONOGA_MAIN().getSymbol(),
                            "expected const, def, val or end of file, got 'bosonoga_main'");
                }
                main = section.block();
                body.append(visit(main));
                // As in the C bosonoga, reaching the end of bosonoga_main fails when
                // it runs, at what follows its block.
                if (!(main.statement().getLast() instanceof BosonogaParser.ReturnStatementContext)) {
                    body.append(INDENT).append("runtime::fail(").append(where(next(main.getStop())))
                            .append(", \"the program ended without a return\");\n");
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
        for (var array : arrays.values()) {
            if (!array.isCalled()[0]) {
                throw error(array.name(), "'%s' is never read".formatted(array.name().getText()));
            }
        }
        for (var variable : globalScope.values()) {
            if (!variable.isRead) {
                throw error(variable.declaration,
                        "'%s' is never read".formatted(variable.declaration.getText()));
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
        var value = context.value() != null ? expression(context.value()) : length(context.arrayLength());
        constants.put(name.getText(), new Constant(name, value));
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
            var cpp = declare(variable.name(), variable.kind(), false, false).cpp;
            lines.append(INDENT.repeat(depth)).append(variable.line().apply(cpp)).append('\n');
        }
        for (var statement : context.statement()) {
            lines.append(INDENT.repeat(depth)).append(visit(statement)).append('\n');
        }
        depth--;
        var scope = scopes.pop();
        for (var variable : scope.values()) {
            if (variable.isNew) {
                throw error(context.DO().getSymbol(), "'%s' is new, and is unset before the end of its block"
                        .formatted(variable.declaration.getText()));
            }
        }
        if (inGlobal && depth == 0) {
            // Later sections read them, so they are checked at the end of the program.
            scopes.push(scope);
            globalScope = scope;
            return lines.toString();
        }
        for (var variable : scope.values()) {
            if (!variable.isRead) {
                throw error(variable.declaration,
                        "'%s' is never read".formatted(variable.declaration.getText()));
            }
        }
        return lines.toString();
    }

    @Override
    public String visitInfoStatement(BosonogaParser.InfoStatementContext context) {
        return "runtime::info(std::cout, " + readingNew(context.value()) + ");";
    }

    @Override
    public String visitDeclareStatement(BosonogaParser.DeclareStatementContext context) {
        var isVal = context.declaration.getType() == BosonogaParser.VAL;
        var name = context.NAME().getSymbol();
        if (context.initializer() instanceof BosonogaParser.ArrayInitializerContext array) {
            if (isVal) {
                throw error(array.ARRAY().getSymbol(),
                        "a val array is top-level, and an array of a block is a var");
            }
            return varArray(name, array);
        }
        var isNew = context.initializer() instanceof BosonogaParser.NewInitializerContext;
        // Declared after its value, so that the value cannot refer to it.
        var initial = switch (context.initializer()) {
            case BosonogaParser.OperationInitializerContext operation -> operation(operation.operation());
            case BosonogaParser.CallInitializerContext call ->
                    takenResult(name, call.callee, call.arguments(), call.result);
            case BosonogaParser.NewInitializerContext created -> created(created);
            case BosonogaParser.LengthInitializerContext length -> length(length.arrayLength());
            case BosonogaParser.ValueInitializerContext value -> flags(value);
            default -> throw new IllegalStateException("no C++ for " + context.getText());
        };
        var variable = declare(name, initial.kind(), isVal, isNew);
        // What new made is not const, as unset frees it.
        return (isVal && !isNew ? "const " : "") + initial.kind().cpp + " " + variable.cpp + " = "
                + initial.cpp() + ";";
    }

    // As in the C bosonoga, new makes a str of the values as info prints them.
    private Expression created(BosonogaParser.NewInitializerContext context) {
        var type = context.type;
        var text = type.getText();
        if (text.equals("xml_str")) {
            throw error(type, "an xml_str is not supported yet, but new str of is");
        }
        if (!text.equals("str")) {
            if (Kind.named(text).isPresent() || LATER_KINDS.contains(text)) {
                throw error(type, "new makes only a str or an xml_str, as in new str of \"A\"_str 1_i32");
            }
            throw error(type, "expected a type, as in new str of, got '%s'".formatted(text));
        }
        return new Expression("runtime::joined(" + readingNew(context.value()) + ")", Kind.STR);
    }

    // The values of info, a comparison or new, which can be what new made.
    private String readingNew(List<BosonogaParser.ValueContext> values) {
        readsNew = true;
        try {
            return values.stream().map(value -> expression(value).cpp()).collect(joining(", "));
        } finally {
            readsNew = false;
        }
    }

    private String varArray(Token name, BosonogaParser.ArrayInitializerContext context) {
        var text = name.getText();
        var columns = types(context.types, "a row of an array has at most %d values"
                .formatted(FUNCTION_VALUES_MAX));
        String initial;
        int rowCount;
        if (context.WITH_LEN_COL() != null) {
            var countValue = context.value();
            var count = expect(countValue, Kind.I32, "with_len_col");
            if (count.whole() == null || count.whole().signum() <= 0) {
                throw error(countValue.getStart(), "with_len_col needs an i32 literal or constant above zero, "
                        + "as the size of an array is known before the program runs");
            }
            rowCount = count.whole().intValue();
            checkArraySize(countValue.getStart(), text, rowCount, columns.size());
            // Each value is zero, false or empty, as in the C bosonoga.
            initial = "(std::size_t{" + rowCount + "})";
        } else {
            var rows = rows(context.rows(), text, columns, true);
            rowCount = rows.size();
            initial = " = {" + String.join(", ", rows) + "}";
        }
        var variable = declare(name, Kind.I32, false, false);
        variable.array = new Function(name, List.of(Kind.I32), columns, new boolean[1], variable.cpp, true,
                rowCount);
        return rowsType(columns) + " " + variable.cpp + initial + ";";
    }

    // A top-level val is an array of literal rows, which the C bosonoga declares
    // as it does a function.
    private String defineArray(BosonogaParser.TopValDeclarationContext context) {
        var name = context.NAME().getSymbol();
        var text = name.getText();
        checkNotSetting(name);
        if (functions.containsKey(text)) {
            throw error(name, "function '%s' already exists".formatted(text));
        }
        if (arrays.containsKey(text)) {
            throw error(name, "array '%s' already exists".formatted(text));
        }
        if (constants.containsKey(text)) {
            throw error(name, "'%s' is already a constant".formatted(text));
        }
        if (find(name).isPresent()) {
            throw error(name, "variable '%s' already exists".formatted(text));
        }
        if (!(context.initializer() instanceof BosonogaParser.ArrayInitializerContext array)) {
            throw error(context.initializer().getStart(), "a top-level val is an array, as in val task is "
                    + "array 0_i32, and const binds a value");
        }
        var columns = types(array.types, "a row of an array has at most %d values"
                .formatted(FUNCTION_VALUES_MAX));
        if (array.WITH_LEN_COL() != null) {
            throw error(array.WITH_LEN_COL().getSymbol(),
                    "a val array has literal rows, and only a var array has zeroed ones");
        }
        var rows = rows(array.rows(), text, columns, false);
        var cpp = "a_" + text;
        arrays.put(text, new Function(name, List.of(Kind.I32), columns, new boolean[1], cpp, true, rows.size()));
        return "const " + rowsType(columns) + " " + cpp + " = {" + String.join(", ", rows) + "};\n";
    }

    private List<String> rows(BosonogaParser.RowsContext context, String name, List<Kind> columns,
                              boolean isVar) {
        var types = valuesShown(columns);
        var rows = new ArrayList<String>();
        for (var row : context.row()) {
            var start = row.ROW().getSymbol();
            var values = row.value();
            var cells = new ArrayList<String>();
            for (int i = 0; i < columns.size(); i++) {
                if (i == values.size()) {
                    throw error(start, "'%s' has rows of %s, but this row has %d".formatted(name, types, i));
                }
                var value = values.get(i);
                if (value instanceof BosonogaParser.NameValueContext named
                        && !constants.containsKey(named.getText()) && !isSetting(named.getText())) {
                    throw error(value.getStart(), ("the values of a row are literals, like 7_i32, so a row "
                            + "cannot have '%s'").formatted(named.getText()));
                }
                cells.add(expect(value, columns.get(i), "a row of '%s'".formatted(name)).cpp());
            }
            if (values.size() > columns.size()) {
                throw error(start, "'%s' has rows of %s, but this row has more".formatted(name, types));
            }
            rows.add("{" + String.join(", ", cells) + "}");
            if (isVar) {
                checkArraySize(start, name, rows.size(), columns.size());
            }
        }
        return rows;
    }

    private void checkArraySize(Token token, String name, long rowCount, int columnCount) {
        var count = rowCount * columnCount;
        if (count > ARRAY_VALUES_MAX) {
            throw error(token, "'%s' would hold %d values, and an array holds at most %d"
                    .formatted(name, count, ARRAY_VALUES_MAX));
        }
    }

    private static String rowsType(List<Kind> columns) {
        return "std::vector<std::tuple<" + columns.stream().map(kind -> kind.cpp).collect(joining(", "))
                + ">>";
    }

    // The number of rows of an array, len_col, or of the values of each row, len_row.
    private Expression length(BosonogaParser.ArrayLengthContext context) {
        var name = context.NAME().getSymbol();
        var variable = find(name).filter(found -> found.array != null);
        Function array;
        if (variable.isPresent()) {
            variable.get().isRead = true;
            array = variable.get().array;
        } else {
            array = arrays.get(name.getText());
            if (array == null) {
                throw error(name, "expected the name of an array, got '%s'".formatted(name.getText()));
            }
            array.isCalled()[0] = true;
        }
        var count = context.count.getType() == BosonogaParser.LEN_COL ? array.rowCount() : array.results().size();
        return new Expression("std::int32_t{" + count + "}", Kind.I32, BigInteger.valueOf(count));
    }

    // A value, which can be flags joined to more flags with shift_or.
    private Expression flags(BosonogaParser.ValueInitializerContext context) {
        var values = context.value();
        var first = expression(values.getFirst());
        // As in the C bosonoga, only flags are followed by shift_or.
        if (values.size() > 1 && first.kind() != Kind.FLAGS) {
            throw error(context.SHIFT_OR(0).getSymbol(), "expected a statement, got 'shift_or'");
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
        checkNotSetting(name);
        if (constants.containsKey(shown)) {
            throw error(name, "'%s' is a constant and cannot be set".formatted(shown));
        }
        var found = find(name);
        if (found.isEmpty() && arrays.containsKey(shown)) {
            throw error(name, "'%s' is a val array and cannot be set, but a var array of a block can"
                    .formatted(shown));
        }
        var target = found.orElseThrow(() -> unknownVariable(name));
        if (target.array != null) {
            if (!(context.setter() instanceof BosonogaParser.RowSetterContext row)) {
                throw error(context.setter().getStart(), ("'%s' is an array, whose rows are set with set %s at "
                        + "INDEX to and one value per column").formatted(shown, shown));
            }
            return rowWrite(name, target.array, row);
        }
        if (target.isNew) {
            throw error(name, "'%s' is new and cannot be set, but it can be unset and declared again"
                    .formatted(shown));
        }
        if (target.isVal) {
            throw error(name, "'%s' is a val and cannot be set".formatted(shown));
        }
        if (context.setter() instanceof BosonogaParser.RowSetterContext row) {
            throw error(row.AT().getSymbol(), "expected to, to_add, to_subtract, to_multiply, to_divide, "
                    + "to_mod or a conversion, got 'at'");
        }
        var assigned = switch (context.setter()) {
            case BosonogaParser.LengthSetterContext length -> {
                if (target.kind != Kind.I32) {
                    throw error(name, "'%s' is %s, but a len is i32".formatted(shown, target.kind.shown));
                }
                yield length(length.arrayLength()).cpp();
            }
            case BosonogaParser.CallSetterContext call -> {
                var result = takenResult(name, call.callee, call.arguments(), call.result);
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
        return target.cpp + " = " + assigned + ";";
    }

    private String rowWrite(Token name, Function array, BosonogaParser.RowSetterContext context) {
        var shown = name.getText();
        var columns = array.results();
        var types = valuesShown(columns);
        var index = expect(context.index, Kind.I32, "the index of '%s'".formatted(shown)).cpp();
        var values = context.value().subList(1, context.value().size());
        var cells = new ArrayList<String>();
        for (int i = 0; i < columns.size(); i++) {
            if (i == values.size()) {
                throw error(name, "'%s' has rows of %s, but this row has %d".formatted(shown, types, i));
            }
            cells.add(expect(values.get(i), columns.get(i), "a row of '%s'".formatted(shown)).cpp());
        }
        if (values.size() > columns.size()) {
            throw error(values.get(columns.size()).getStart(), "'%s' has rows of %s, but this row has more"
                    .formatted(shown, types));
        }
        return "runtime::row(" + where(name) + ", " + array.cpp() + ", \"" + shown + "\", " + index + ") = {"
                + String.join(", ", cells) + "};";
    }

    // As in the C bosonoga, only what new made is unset, in its own block, once it
    // is read.
    @Override
    public String visitUnsetStatement(BosonogaParser.UnsetStatementContext context) {
        var name = context.NAME().getSymbol();
        var shown = name.getText();
        checkNotSetting(name);
        var variable = find(name).orElseThrow(() -> unknownVariable(name));
        if (!variable.isNew) {
            throw error(name, "'%s' is not new, and only what new made is unset".formatted(shown));
        }
        if (scopes.getFirst().get(shown) != variable) {
            throw error(name, "'%s' is unset in the block where it is new".formatted(shown));
        }
        if (!variable.isRead) {
            throw error(variable.declaration, "'%s' is never read".formatted(shown));
        }
        scopes.getFirst().remove(shown);
        return "runtime::unset(" + variable.cpp + ");";
    }

    // The first variable that new made and that is not unset, as the C bosonoga
    // finds it, of the current loop only when in_loop.
    private Optional<Variable> findNew(boolean inLoop) {
        var outermost = scopes.descendingIterator();
        while (outermost.hasNext()) {
            for (var variable : outermost.next().values()) {
                if (variable.isNew && (!inLoop || variable.loopDepth == loopDepth)) {
                    return Optional.of(variable);
                }
            }
        }
        return Optional.empty();
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
                : List.of(new Declared(name, Kind.I32, cpp -> "std::int32_t " + cpp + " = " + counter + ";")));
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
        var owner = findNew(true);
        if (owner.isPresent()) {
            throw error(jump, "unset '%s' before %s".formatted(owner.get().declaration.getText(), jump.getText()));
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
        var owner = findNew(false);
        if (owner.isPresent()) {
            throw error(start, "unset '%s' before return".formatted(owner.get().declaration.getText()));
        }
        if (inGlobal) {
            throw error(start, "only bosonoga_main and gfx_main can return");
        }
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
        if (isSetting(text)) {
            throw error(name, "expected a function name, got '%s'".formatted(text));
        }
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
        // The variables of bosonoga_global are not seen.
        var outerScopes = List.copyOf(scopes);
        scopes.clear();
        scopes.push(new LinkedHashMap<>());
        var parameters = new ArrayList<Kind>();
        var cppParameters = new StringBuilder("const runtime::Where &called_at");
        for (var parameter : context.parameters) {
            var kind = parameterKind(parameter);
            if (parameters.size() == FUNCTION_VALUES_MAX) {
                throw error(parameter, "a function takes at most %d values".formatted(FUNCTION_VALUES_MAX));
            }
            var variable = declare(parameter, kind, false, false);
            parameters.add(kind);
            cppParameters.append(", ").append(kind.cpp).append(' ').append(variable.cpp);
        }
        List<Kind> results = List.of();
        if (context.RET() != null) {
            results = types(context.results, "a function returns at most %d values".formatted(FUNCTION_VALUES_MAX));
        } else if (context.RET_VOID() == null) {
            throw error(context.block().getStart(), ("'%s' must say what it returns: ret and its types, "
                    + "or ret_void for nothing").formatted(text));
        }
        var function = new Function(name, List.copyOf(parameters), results, new boolean[1],
                cppFunctionName(text), false, 0);
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
        scopes.addAll(outerScopes);
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

    // The types of the values of a function or of a row, numbered from 0 in order.
    private List<Kind> types(List<Token> types, String tooMany) {
        var kinds = new ArrayList<Kind>();
        for (var type : types) {
            var indexed = indexed(type).orElseThrow(() -> error(type, ("expected a type with its index, like "
                    + "0_i32, of i32, i64, u32, u64, f32, f64, flags, color, str or bool, got '%s'")
                    .formatted(type.getText())));
            if (kinds.size() == FUNCTION_VALUES_MAX) {
                throw error(type, tooMany);
            }
            if (indexed.index() != kinds.size()) {
                throw error(type, "the values are numbered from 0 in order, so '%s' is %d_%s"
                        .formatted(type.getText(), kinds.size(), indexed.kind().shown));
            }
            kinds.add(indexed.kind());
        }
        return List.copyOf(kinds);
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
        var call = call(function, context.callee, context.arguments());
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
                    .orElseThrow(() -> error(position, "'%s' %s %s, so it has no %s"
                            .formatted(name, function.gives(), kindsNamed(results), shownPosition)));
            if (previous != null && taken.index() <= previous.index()) {
                throw error(position, ("the values are taken once each, by increasing index, so %s cannot "
                        + "follow %d_%s").formatted(shownPosition, previous.index(), previous.kind().shown));
            }
            previous = taken;
            // A row is a tuple even of one value, and a function of one value is not.
            var value = !function.isArray() && results.size() == 1
                    ? result
                    : "std::get<" + taken.index() + ">(" + result + ")";
            declared.add(new Declared(binding.NAME().getSymbol(), taken.kind(),
                    cpp -> taken.kind().cpp + " " + cpp + " = " + value + ";"));
        }
        var block = block(context.block(), List.of("const auto " + result + " = " + call + ";"), declared);
        return "{\n" + block + INDENT.repeat(depth) + "}";
    }

    // The one value of a call that var and set ... to take, as with ret 0_i32.
    private Expression takenResult(Token target, Token callee, BosonogaParser.ArgumentsContext arguments,
                                   Token result) {
        var function = callable(callee);
        var name = callee.getText();
        var call = call(function, callee, arguments);
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
            throw error(result, "'%s' %s %s, so its call ends with ret 0_%s, not '%s'"
                    .formatted(name, function.gives(), kind.shown, kind.shown, result.getText()));
        }
        return new Expression(function.isArray() ? "std::get<0>(" + call + ")" : call, kind);
    }

    private BosonogaException needsBlock(Token token, Function function) {
        var results = function.results();
        return error(token, ("'%s' %s %s, which %scall ... with ret takes for its block, each named, "
                + "as in with ret 0_%s as value").formatted(function.name().getText(), function.gives(),
                kindsNamed(results), results.size() == 1 ? "var, set ... to or " : "",
                results.getFirst().shown));
    }

    // A function that only calls itself is not used, as in the C bosonoga.
    private Function callable(Token callee) {
        var variable = find(callee).filter(found -> found.array != null);
        if (variable.isPresent()) {
            variable.get().isRead = true;
            return variable.get().array;
        }
        var function = functions.get(callee.getText());
        if (function == null) {
            function = arrays.get(callee.getText());
        }
        if (function == null) {
            throw error(callee, "unknown function '%s'".formatted(callee.getText()));
        }
        if (function != currentFunction) {
            function.isCalled()[0] = true;
        }
        return function;
    }

    private String call(Function function, Token callee, BosonogaParser.ArgumentsContext arguments) {
        var name = callee.getText();
        var values = arguments == null ? List.<BosonogaParser.ValueContext>of() : arguments.value();
        if (function.isArray()) {
            if (arguments == null || arguments.AT() == null) {
                throw error(arguments == null ? next(callee) : arguments.getStart(), ("'%s' is an array, "
                        + "which is read at one i32 index, as in %s at 0_i32").formatted(name, name));
            }
            if (values.size() > 1) {
                throw error(values.get(1).getStart(), ("'%s' is an array, which is read at one i32 index, "
                        + "but is given more").formatted(name));
            }
            var index = expect(values.getFirst(), Kind.I32, "the index of '%s'".formatted(name)).cpp();
            return "runtime::row(" + where(callee) + ", " + function.cpp() + ", \"" + name + "\", " + index + ")";
        }
        if (arguments != null && arguments.AT() != null) {
            throw error(arguments.AT().getSymbol(),
                    "'%s' is a function, and only an array is read at an index".formatted(name));
        }
        var of = arguments == null ? null : arguments.OF();
        var parameters = function.parameters();
        var shownParameters = valuesShown(parameters);
        if (parameters.isEmpty() && of != null) {
            throw error(of.getSymbol(), "'%s' takes no values, so its call has no of".formatted(name));
        }
        if (!parameters.isEmpty() && of == null) {
            throw error(next(callee), "'%s' takes %s, which follow of, as in %s of ..."
                    .formatted(name, shownParameters, name));
        }
        var cpp = new StringBuilder(where(callee));
        for (int i = 0; i < parameters.size(); i++) {
            if (i == values.size()) {
                throw error(next(values.getLast().getStop()), "'%s' takes %s, but is given %d"
                        .formatted(name, shownParameters, i));
            }
            cpp.append(", ").append(expect(values.get(i), parameters.get(i), name).cpp());
        }
        if (values.size() > parameters.size()) {
            throw error(values.get(parameters.size()).getStart(), "'%s' takes %s, but is given more"
                    .formatted(name, shownParameters));
        }
        return function.cpp() + "(" + cpp + ")";
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
        readsNew = true;
        Expression left;
        Expression right;
        try {
            left = expression(context.value(0));
            right = context.compare != null ? expression(context.value(1)) : null;
        } finally {
            readsNew = false;
        }
        String cpp;
        if (context.compare != null) {
            cpp = comparison(context.compare, left, right);
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
        if (kind == Kind.COLOR) {
            throw error(compare, "colors cannot be compared");
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
        try {
            return NumberLiteral.parse(literal.getText());
        } catch (NumberLiteral.Invalid invalid) {
            throw error(literal, invalid.getMessage());
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

    private Variable declare(Token name, Kind kind, boolean isVal, boolean isNew) {
        checkNewName(name);
        var text = name.getText();
        var count = declarations.merge(text, 1, Integer::sum);
        // The prefix keeps a bosonoga name from being a C++ keyword, such as new.
        var cpp = count == 1 ? "v_" + text : "v" + count + "_" + text;
        var variable = new Variable(name, kind, isVal, isNew, loopDepth, cpp);
        scopes.getFirst().put(text, variable);
        return variable;
    }

    private void checkNewName(Token name) {
        checkNotSetting(name);
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
        if (arrays.containsKey(name.getText())) {
            throw error(name, "'%s' is already an array".formatted(name.getText()));
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
        if (isSetting(name.getText())) {
            return setting(name);
        }
        if (functions.containsKey(name.getText())) {
            throw error(name, "'%s' is a function, which is called with var, set ... to or call"
                    .formatted(name.getText()));
        }
        var variable = find(name);
        if (arrays.containsKey(name.getText()) || variable.filter(found -> found.array != null).isPresent()) {
            throw error(name, "'%s' is an array, which is read with var, set ... to or call"
                    .formatted(name.getText()));
        }
        var found = variable.orElseThrow(() -> unknownVariable(name));
        // A copy would outlive the unset of what it points into, as in the C bosonoga.
        if (found.isNew && !readsNew) {
            throw error(name, "'%s' is new, so it %s".formatted(name.getText(), NEW_READS));
        }
        found.isRead = true;
        return new Expression(found.cpp, found.kind);
    }

    // A name of regina.ini, as in color_night, is a literal, and is not a name
    // of the program. As in the C bosonoga, a name that regina.ini does not have
    // is a name of the program.
    private boolean isSetting(String text) {
        return hasSetting(settings.colors, COLOR_PREFIX, text) || hasSetting(settings.globals, GLOBAL_PREFIX, text)
                || hasSetting(settings.keys, KEY_PREFIX, text) || hasSetting(settings.fonts, FONT_PREFIX, text)
                || hasSetting(settings.icons, ICON_PREFIX, text) || hasSetting(settings.dirs, DIR_PREFIX, text);
    }

    private static boolean hasSetting(Map<String, ?> settings, String prefix, String text) {
        return text.startsWith(prefix) && settings.containsKey(text.substring(prefix.length()));
    }

    private Expression setting(Token name) {
        var text = name.getText();
        if (hasSetting(settings.colors, COLOR_PREFIX, text)) {
            var color = settings.colors.get(text.substring(COLOR_PREFIX.length()));
            return new Expression("runtime::Color{0x%06XU}".formatted(color), Kind.COLOR);
        }
        if (hasSetting(settings.globals, GLOBAL_PREFIX, text)) {
            return settings.globals.get(text.substring(GLOBAL_PREFIX.length()));
        }
        var kind = hasSetting(settings.keys, KEY_PREFIX, text) ? "input_key"
                : hasSetting(settings.fonts, FONT_PREFIX, text) ? "font"
                : hasSetting(settings.icons, ICON_PREFIX, text) ? "icon"
                : "dir";
        throw error(name, "a value of %s is not supported yet".formatted(kind));
    }

    private void checkNotSetting(Token name) {
        if (isSetting(name.getText())) {
            throw error(name, "expected a variable name, got '%s'".formatted(name.getText()));
        }
    }

    private BosonogaException unknownVariable(Token name) {
        return error(name, "unknown variable '%s'".formatted(name.getText()));
    }

    // Where a runtime error is reported, as in the C bosonoga.
    private String where(Token token) {
        usesSource = true;
        return "{source, " + token.getLine() + ", " + (token.getCharPositionInLine() + 1) + "}";
    }

    private static String cppStringText(String text) {
        return text.replace("\\", "\\\\").replace("\"", "\\\"");
    }

    private BosonogaException error(Token token, String message) {
        return new BosonogaException(sourceName, token.getLine(), token.getCharPositionInLine() + 1,
                message);
    }
}
