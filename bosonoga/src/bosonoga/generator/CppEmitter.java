package bosonoga.generator;

import static bosonoga.generator.CppText.INDENT;
import static bosonoga.generator.Tokens.isEnd;
import static bosonoga.generator.Tokens.rawShown;
import static bosonoga.generator.Tokens.shown;
import static java.util.stream.Collectors.joining;

import bosonoga.generator.Symbols.Constant;
import bosonoga.generator.Symbols.Declared;
import bosonoga.generator.Symbols.Function;
import bosonoga.generator.Symbols.Indexed;
import bosonoga.generator.Symbols.Variable;
import java.math.BigInteger;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.function.Supplier;
import java.util.regex.Pattern;
import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.TokenStream;

// Checks a program as the C bosonoga parser does, in the order that it reads
// it, and writes its C++. Each visit of a statement gives its C++ line(s),
// and an error is thrown as soon as it is found.
final class CppEmitter extends BosonogaBaseVisitor<String> {
    private static final String SHIFT_PREFIX = "shift_";
    private static final int SHIFT_BIT_MAX = 31;
    private static final String LOOP_PREFIX = "to_";
    private static final String FUNCTION_SUFFIX = "_fn";
    private static final int FUNCTION_VALUES_MAX = 16;
    private static final int ARRAY_VALUES_MAX = 65536;
    // A type with its index, as in 0_i32, which names a value of a function.
    private static final Pattern INDEXED = Pattern.compile("(0|[1-9][0-9]{0,8})_([A-Za-z0-9_]+)");
    private static final String NEW_READS = "is only read by info, a comparison, new, to_join, io_write, "
            + "an xml_ statement or str_parse, and is not copied";

    // What a given statement gives its block: the kinds by index, other kinds
    // an index can be taken as, and the indices of the text that is freed after
    // the block.
    private record Gives(List<Kind> results, Map<Integer, Set<Kind>> also, Set<Integer> freed,
                         boolean writesXml) {
    }

    private final String sourceName;
    private final String functionName;
    private final TokenStream tokens;
    private final ImportingTokenSource importing;
    private final BosonogaException cut;
    private final Locations locations;
    private final Diagnostics diagnostics;
    private final Modules modules;
    private final SettingValues settings;

    // The C++ of the functions and arrays, and of bosonoga_main.
    private final StringBuilder definitions = new StringBuilder();
    private final StringBuilder body = new StringBuilder();

    private final Map<String, Constant> constants = new LinkedHashMap<>();
    private final Map<String, Function> functions = new LinkedHashMap<>();
    private final Map<String, Function> arrays = new LinkedHashMap<>();
    private final List<Function> declaredFunctions = new ArrayList<>();
    // How often each name is declared, so that a name declared again after unset
    // has a C++ name of its own.
    private final Map<String, Integer> declarations = new LinkedHashMap<>();

    private BosonogaParser.BlockContext main;
    private boolean hasGlobal;
    private boolean inGlobal;
    private boolean isGfx;
    private boolean hasConsoleMain;
    private boolean inLiteralRow;
    private String section = "bosonoga_main";
    private final Map<String, String> gfxSections = new LinkedHashMap<>();
    // The variables at the top of bosonoga_global, which later sections see.
    private Map<String, Variable> globalScope = Map.of();

    // Where the visit is: the variables of each open block, innermost first.
    private final Deque<Map<String, Variable>> scopes = new ArrayDeque<>();
    private Function currentFunction;
    private int depth;
    private int loopDepth;
    private boolean isPub;
    private boolean isPubVariable;
    private boolean readsNew;

    // For the C++ names of loop counters and of results.
    private int loopCount;
    private int resultCount;

    CppEmitter(String sourceName, String functionName, TokenStream tokens, ImportingTokenSource importing,
               Settings settings) {
        this(sourceName, functionName, tokens, importing, settings, null);
    }

    // With the syntax error that the CUT of a program cut there throws.
    CppEmitter(String sourceName, String functionName, TokenStream tokens, ImportingTokenSource importing,
               Settings settings, BosonogaException cut) {
        this.sourceName = sourceName;
        this.functionName = ProgramName.cpp(functionName);
        this.tokens = tokens;
        this.importing = importing;
        this.cut = cut;
        locations = new Locations(sourceName);
        diagnostics = new Diagnostics(tokens, locations, cut);
        modules = new Modules(sourceName);
        this.settings = new SettingValues(settings);
    }

    // As in the C bosonoga, a constant is seen only after it is declared.
    @Override
    public String visitProgram(BosonogaParser.ProgramContext context) {
        items(context.item());
        if (main == null) {
            throw error(context.EOF().getSymbol(), isGfx
                    ? "a gfx program needs a gfx_main block" : "a program needs a bosonoga_main block");
        }
        for (var constant : constants.values()) {
            if (!constant.isUsed) {
                throw error(constant.declaration,
                        "'%s' is never used".formatted(rawShown(constant.declaration)));
            }
        }
        // Functions and arrays in the order they are declared, as in the C bosonoga.
        for (var function : declaredFunctions) {
            if (!function.isCalled()) {
                throw error(function.name(), "'%s' is never %s".formatted(rawShown(function.name()),
                        function.isArray() ? "read" : "called"));
            }
        }
        for (var variable : globalScope.values()) {
            if (!variable.isRead) {
                throw neverRead(variable.declaration);
            }
        }
        if (isGfx) {
            var callbacks = new StringBuilder(body);
            for (var entry : gfxSections.entrySet()) {
                callbacks.append("  [[maybe_unused]] auto ").append(entry.getKey()).append(" = [&]()")
                        .append(entry.getKey().equals("gfx_main") ? " -> int" : "")
                        .append(" {\n").append(entry.getValue()).append("  };\n");
            }
            var checked = new StringBuilder(definitions)
                    .append("[[maybe_unused]] void gfx_sections(runtime::gfx::Context &gfx_context) {\n")
                    .append("  static_cast<void>(gfx_context);\n").append(callbacks).append("}\n");
            return CppText.program(sourceName, locations.declarations(), checked,
                    "  throw runtime::Error(\"the console runner requires bosonoga_main\");\n", functionName);
        }
        return CppText.program(sourceName, locations.declarations(), definitions, body, functionName);
    }

    private void items(List<BosonogaParser.ItemContext> items) {
        for (var item : items) {
            if (declaration(item.constantDeclaration(), item.functionDefinition(), item.topValDeclaration())) {
                continue;
            }
            if (item.globalSection() != null) {
                globalSection(item.globalSection());
            } else if (item.mainSection() != null) {
                mainSection(item.mainSection());
            } else if (item.gfxSection() != null) {
                gfxSection(item.gfxSection());
            } else if (item.fileItem() != null) {
                fileItem(item.fileItem());
            } else if (item.pubItem() != null) {
                pubItem(item.pubItem());
            } else if (item.CUT() != null) {
                throw cut;
            } else {
                otherItem(item.other);
            }
        }
    }

    // What can be pub: a constant, a function or an array.
    private boolean declaration(BosonogaParser.ConstantDeclarationContext constant,
                                BosonogaParser.FunctionDefinitionContext function,
                                BosonogaParser.TopValDeclarationContext array) {
        if (constant != null) {
            declareConstant(constant);
        } else if (function != null) {
            definitions.append(defineFunction(function)).append('\n');
        } else if (array != null) {
            definitions.append(defineArray(array)).append('\n');
        } else {
            return false;
        }
        return true;
    }

    // Its statements are before those of bosonoga_main, in the same C++ scope.
    private void globalSection(BosonogaParser.GlobalSectionContext section) {
        if (main != null || hasGlobal || isGfx) {
            throw error(section.BOSONOGA_GLOBAL().getSymbol(), "bosonoga_global must be the first section");
        }
        hasGlobal = true;
        this.section = "bosonoga_global";
        inGlobal = true;
        body.append(visit(section.block()));
        inGlobal = false;
    }

    private void mainSection(BosonogaParser.MainSectionContext section) {
        if (isGfx) {
            otherItem(section.BOSONOGA_MAIN().getSymbol());
        }
        if (main != null) {
            throw error(section.BOSONOGA_MAIN().getSymbol(),
                    "expected const, def, val or end of file, got 'bosonoga_main'");
        }
        main = section.block();
        hasConsoleMain = true;
        this.section = "bosonoga_main";
        body.append(visit(main));
        // As in the C bosonoga, reaching the end of bosonoga_main fails when
        // it runs, at what follows its block.
        if (!(main.statement().getLast() instanceof BosonogaParser.ReturnStatementContext)) {
            body.append(INDENT).append("runtime::fail(").append(where(next(main.getStop())))
                    .append(", \"the program ended without a return\");\n");
        }
    }

    private void gfxSection(BosonogaParser.GfxSectionContext context) {
        var token = context.section;
        if (hasConsoleMain) otherItem(token);
        var name = token.getText();
        if (gfxSections.containsKey(name)) {
            throw error(token, "there is already a %s block".formatted(name));
        }
        isGfx = true;
        section = name;
        if (name.equals("gfx_main")) main = context.block();
        var code = visit(context.block());
        if (name.equals("gfx_main")
                && !(context.block().statement().getLast() instanceof BosonogaParser.ReturnStatementContext)) {
            code += INDENT + "runtime::fail(" + where(next(context.block().getStop()))
                    + ", \"the program ended without a return\");\n";
        }
        gfxSections.put(name, code);
    }

    // As in the C bosonoga, a module has a scope of its own, inside the scope
    // that reads it, and the items of a raw import are those of its reader.
    private void fileItem(BosonogaParser.FileItemContext context) {
        var keyword = context.keyword;
        if (context.RAW_START() != null) {
            // Its tokens follow this item directly and can continue in the
            // reader after raw EOF, just as the C tokenizer resumes there.
            return;
        }
        if (context.FILE_START() == null) {
            var failure = importing.failure(keyword);
            if (failure != null) {
                throw failure;
            }
            var got = context.value() != null ? context.value().getStart() : next(keyword);
            if (got.getType() == BosonogaParser.STRING) {
                string(got);
            }
            throw error(got, "expected a file name, as in %s \"lorem.bosonoga\"_str, got %s"
                    .formatted(keyword.getText(), shown(got)));
        }
        var outer = modules.enter(locations.path(context.FILE_END().getSymbol()));
        items(context.item());
        modules.leave(outer);
    }

    private void pubItem(BosonogaParser.PubItemContext context) {
        var pub = context.PUB().getSymbol();
        if (modules.isProgram()) {
            throw error(pub, "pub is only in a module, a file read with mod");
        }
        isPub = true;
        if (!declaration(context.constantDeclaration(), context.functionDefinition(),
                context.topValDeclaration())) {
            throw error(next(pub), "expected const, def or val after pub, got %s".formatted(shown(next(pub))));
        }
        isPub = false;
    }

    // What cannot start an item, in the words of the C bosonoga.
    private void otherItem(Token token) {
        if (token.getType() == BosonogaParser.ARRAY) {
            throw error(token, "an array is declared as val NAME is array, as in val task is array 0_i32");
        }
        if (token.getType() == BosonogaParser.VAR) {
            throw error(token, "a var is a variable of a block, and a top-level array is a val");
        }
        if (hasConsoleMain) {
            throw error(token, "expected const, def, val or end of file, got %s".formatted(shown(token)));
        }
        if (isGfx) {
            throw error(token, ("expected const, def, val, gfx_init, gfx_frame, gfx_cleanup, "
                    + "gfx_key_down or gfx_main, got %s")
                    .formatted(shown(token)));
        }
        throw error(token, ("expected const, def, val, %sbosonoga_main, gfx_init, gfx_frame, gfx_cleanup, "
                + "gfx_key_down or gfx_main, got %s").formatted(hasGlobal ? "" : "bosonoga_global, ", shown(token)));
    }

    private void declareConstant(BosonogaParser.ConstantDeclarationContext context) {
        var name = context.NAME().getSymbol();
        checkNewName(name);
        var value = context.value() != null ? expression(context.value()) : length(context.arrayLength());
        // As in the C parser, a const holds a literal, not a reference to a
        // variable of bosonoga_global. Resolve the value first so that name,
        // visibility and ownership errors keep their usual precedence.
        if (context.value() instanceof BosonogaParser.GfxValueContext
                || context.value() instanceof BosonogaParser.NameValueContext named
                && find(named.NAME().getSymbol()).isPresent()) {
            throw error(context.value().getStart(), "a constant needs a literal or an earlier constant");
        }
        constants.put(name.getText(), new Constant(name, value));
        declareHome(name);
    }

    @Override
    public String visitBlock(BosonogaParser.BlockContext context) {
        return block(context, List.of(), List.of());
    }

    // A block can start with lines of its own, and with the variables that a loop
    // or a call declares for it.
    private String block(BosonogaParser.BlockContext context, List<String> prelude,
                         List<Declared> declared) {
        if (context.CUT() != null) {
            throw cut;
        }
        scopes.push(new LinkedHashMap<>());
        depth++;
        var lines = new StringBuilder();
        for (var line : prelude) {
            lines.append(INDENT.repeat(depth)).append(line).append('\n');
        }
        for (var variable : declared) {
            var cpp = declare(variable.name(), variable.kind(), false, variable.freedBy() != null,
                    variable.freedBy()).cpp;
            lines.append(INDENT.repeat(depth)).append(variable.line().apply(cpp)).append('\n');
        }
        for (var statement : context.statement()) {
            lines.append(INDENT.repeat(depth)).append(visit(statement)).append('\n');
        }
        depth--;
        var scope = scopes.pop();
        for (var variable : scope.values()) {
            if (variable.isNew && variable.freedBy == null) {
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
                throw neverRead(variable.declaration);
            }
        }
        return lines.toString();
    }

    // As in the C bosonoga, a module's bosonoga_global can share a variable at its
    // top with the program that reads the module.
    @Override
    public String visitPubStatement(BosonogaParser.PubStatementContext context) {
        var pub = context.PUB().getSymbol();
        if (modules.isProgram()) {
            throw error(pub, "pub is only in a module, a file read with mod");
        }
        if (!inGlobal || depth != 1 || currentFunction != null) {
            throw error(pub, "pub is before a top-level const, def or val, or a var or val at the top of "
                    + "bosonoga_global");
        }
        if (!(context.statement() instanceof BosonogaParser.DeclareStatementContext declare)) {
            var got = next(pub);
            throw error(got, "expected var or val after pub, got %s".formatted(shown(got)));
        }
        isPubVariable = true;
        try {
            return visit(declare);
        } finally {
            isPubVariable = false;
        }
    }

    @Override
    public String visitCutStatement(BosonogaParser.CutStatementContext context) {
        throw cut;
    }

    @Override
    public String visitInfoStatement(BosonogaParser.InfoStatementContext context) {
        var values = context.value();
        return "runtime::info(std::cout, "
                + readingNew(() -> values.stream().map(value -> expression(value).cpp()).collect(joining(", ")))
                + ");";
    }

    @Override
    public String visitGfxStatement(BosonogaParser.GfxStatementContext context) {
        return new GfxStatements(this::expect, this::gfxText, this::next, this::where, this::error)
                .emit(context, currentFunction == null && section.equals("gfx_frame"),
                        currentFunction == null && section.equals("gfx_main"));
    }

    private Expression gfxText(BosonogaParser.ValueContext value) {
        readsNew = true;
        try {
            return expression(value);
        } finally {
            readsNew = false;
        }
    }

    @Override
    public String visitDeclareStatement(BosonogaParser.DeclareStatementContext context) {
        var isVal = context.declaration.getType() == BosonogaParser.VAL;
        var name = context.NAME().getSymbol();
        // Validate the name before reading its initializer, but do not make
        // the variable visible until after the initializer has been checked.
        checkNewName(name);
        if (context.CUT() != null) {
            throw cut;
        }
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
                    takenResult(name, call.callee, call.arguments(), call.takeOne());
            case BosonogaParser.ValueInitializerContext value when isCalled(List.of(value.value())) ->
                    takenResult(name, value.value().getStart(), null, null);
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

    // As in the C bosonoga, new makes a str of the values as info prints them,
    // or an xml_str of an element or of content.
    private Expression created(BosonogaParser.NewInitializerContext context) {
        var type = context.type;
        var text = type.getText();
        if (text.equals("xml_str")) {
            return createdXml(context);
        }
        if (!text.equals("str")) {
            if (Kind.named(text).isPresent()) {
                throw error(type, "new makes only a str or an xml_str, as in new str of \"A\"_str 1_i32");
            }
            throw error(type, "expected a type, as in new str of, got '%s'".formatted(text));
        }
        if (!context.heads.isEmpty() || context.OF() == null) {
            var got = context.heads.isEmpty() ? next(type) : context.heads.getFirst().getStart();
            throw error(got, "expected of, got %s".formatted(shown(got)));
        }
        if (context.values.isEmpty()) {
            throw expectedValue(next(context.OF().getSymbol()));
        }
        return readingNew(() -> {
            var parts = new StringBuilder("runtime::NewStr()");
            for (var value : context.values) {
                parts.append(".part(").append(where(value.getStart())).append(", ").append(expression(value).cpp())
                        .append(')');
            }
            return new Expression(parts.append(".take()").toString(), Kind.STR);
        });
    }

    // Each part is checked where it is written, as the C bosonoga joins one at a time.
    private Expression createdXml(BosonogaParser.NewInitializerContext context) {
        var heads = context.heads;
        var join = new StringBuilder("runtime::XmlJoin()");
        return readingNew(() -> {
            if (heads.isEmpty()) {
                if (context.OF() == null) {
                    throw expectedElementOrOf(next(context.type));
                }
                xmlContent(context.values, next(context.OF().getSymbol()), join);
                return new Expression(join.append(".take()").toString(), Kind.XML_STR);
            }
            var first = heads.getFirst();
            if (!(first instanceof BosonogaParser.NameValueContext word && word.getText().equals("element"))) {
                throw expectedElementOrOf(first.getStart());
            }
            var start = first.getStart();
            var element = xmlName(heads, 1, "an element", after(heads, 1, start));
            join.append(".open(").append(where(start)).append(", \"").append(element).append("\")");
            var attributes = new ArrayList<String>();
            var i = 2;
            while (i < heads.size() && isStrLiteral(heads.get(i))) {
                var nameToken = heads.get(i).getStart();
                var attribute = literalText(nameToken);
                if (attributes.contains(attribute)) {
                    throw error(nameToken, "the attribute %s is already given".formatted(rawShown(nameToken)));
                }
                attributes.add(attribute);
                xmlName(heads, i, "an attribute", nameToken);
                join.append(".name(").append(where(nameToken)).append(", \"").append(attribute).append("\")");
                i++;
                if (i == heads.size()) {
                    var got = after(heads, i, start);
                    throw error(got, "expected the value of the attribute, got %s".formatted(shown(got)));
                }
                var value = heads.get(i++);
                join.append(".value(").append(where(value.getStart())).append(", ").append(expression(value).cpp())
                        .append(')');
            }
            if (i < heads.size()) {
                xmlName(heads, i, "an attribute", heads.get(i).getStart());
            }
            if (context.OF() == null) {
                join.append(".empty(").append(where(start)).append(')');
            } else {
                join.append(".start(").append(where(start)).append(')');
                xmlContent(context.values, next(context.OF().getSymbol()), join);
                join.append(".close(").append(where(start)).append(", \"").append(element).append("\")");
            }
            return new Expression(join.append(".take()").toString(), Kind.XML_STR);
        });
    }

    private BosonogaException expectedElementOrOf(Token got) {
        return error(got, "expected element or of, as in new xml_str element \"action\"_str, got %s"
                .formatted(shown(got)));
    }

    // An xml_str is joined as it is, and any other value as the text that info
    // prints, escaped.
    private StringBuilder xmlContent(List<BosonogaParser.ValueContext> values, Token missing, StringBuilder join) {
        if (values.isEmpty()) {
            throw expectedValue(missing);
        }
        for (var value : values) {
            var expression = expression(value);
            join.append(expression.kind() == Kind.XML_STR ? ".raw(" : ".text(").append(where(value.getStart()))
                    .append(", ").append(expression.cpp()).append(')');
        }
        return join;
    }

    // The name of an element or of an attribute is a str literal, as the C++
    // names it as it is.
    private String xmlName(List<BosonogaParser.ValueContext> values, int index, String what, Token missing) {
        var token = index < values.size() ? values.get(index).getStart() : missing;
        if (index < values.size() && values.get(index) instanceof BosonogaParser.StringValueContext) {
            string(token);
        }
        if (index >= values.size() || !isStrLiteral(values.get(index))) {
            throw error(token, "expected the name of %s, a str literal like \"action\"_str, got %s"
                    .formatted(what, shown(token)));
        }
        var name = literalText(token);
        if (!Xml.isName(name)) {
            throw error(token, ("%s is not the name of %s, which starts with an ASCII letter or _, and has letters, "
                    + "digits, _, - and .").formatted(rawShown(token), what));
        }
        return name;
    }

    private static boolean isStrLiteral(BosonogaParser.ValueContext value) {
        var text = value.getText();
        return value instanceof BosonogaParser.StringValueContext
                && text.substring(text.lastIndexOf('"') + 1).equals(StringLiteral.STRING_SUFFIX);
    }

    private static String literalText(Token literal) {
        var text = literal.getText();
        return text.substring(1, text.lastIndexOf('"'));
    }

    // The token after the values before index, or after start if there are none.
    private Token after(List<BosonogaParser.ValueContext> values, int index, Token start) {
        return index == 0 ? next(start) : next(values.get(Math.min(index, values.size()) - 1).getStop());
    }

    private BosonogaException expectedValue(Token got) {
        return error(got, "expected a value, got %s".formatted(shown(got)));
    }

    // Of what reads values that can be what new made, as info, a comparison
    // or new does.
    private <T> T readingNew(Supplier<T> reading) {
        readsNew = true;
        try {
            return reading.get();
        } finally {
            readsNew = false;
        }
    }

    private String varArray(Token name, BosonogaParser.ArrayInitializerContext context) {
        var text = name.getText();
        var columns = types(context.types, "a row of an array has at most %d values"
                .formatted(FUNCTION_VALUES_MAX));
        if (context.CUT() != null) {
            throw cut;
        }
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
            var named = columns.stream().filter(Kind::isNamed).findFirst();
            if (named.isPresent()) {
                throw error(context.WITH_LEN_COL().getSymbol(), ("a zeroed row cannot have %s, which has no zero, "
                        + "but literal rows can").formatted(named.get().shown));
            }
            // Each value is zero, false or empty, as in the C bosonoga.
            initial = "(std::size_t{" + rowCount + "})";
        } else {
            var rows = rows(context.rows(), text, columns, true);
            rowCount = rows.size();
            initial = " = {" + String.join(", ", rows) + "}";
        }
        var variable = declare(name, Kind.I32, false, false);
        variable.array = new Function(name, List.of(Kind.I32), columns, variable.cpp, true,
                rowCount);
        return CppText.rowsType(columns) + " " + variable.cpp + initial + ";";
    }

    // A top-level val is an array of literal rows, which the C bosonoga declares
    // as it does a function.
    private String defineArray(BosonogaParser.TopValDeclarationContext context) {
        var name = context.NAME().getSymbol();
        var text = name.getText();
        // The C parser takes a top-level val name with parse_new_name too.
        checkNewName(name);
        if (!(context.initializer() instanceof BosonogaParser.ArrayInitializerContext array)) {
            throw error(context.initializer().getStart(), "a top-level val is an array, as in val task is "
                    + "array 0_i32, and const binds a value");
        }
        var columns = types(array.types, "a row of an array has at most %d values"
                .formatted(FUNCTION_VALUES_MAX));
        if (array.CUT() != null) {
            throw cut;
        }
        if (array.WITH_LEN_COL() != null) {
            throw error(array.WITH_LEN_COL().getSymbol(),
                    "a val array has literal rows, and only a var array has zeroed ones");
        }
        var rows = rows(array.rows(), text, columns, false);
        var cpp = "a_" + text;
        var defined = new Function(name, List.of(Kind.I32), columns, cpp, true, rows.size());
        arrays.put(text, defined);
        declaredFunctions.add(defined);
        declareHome(name);
        return "const " + CppText.rowsType(columns) + " " + cpp + " = {" + String.join(", ", rows) + "};\n";
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
                if (value instanceof BosonogaParser.NameValueContext named) {
                    checkNotHidden(named.NAME().getSymbol());
                }
                if (value instanceof BosonogaParser.NameValueContext named
                        && !constants.containsKey(named.getText()) && !settings.has(named.getText())) {
                    throw error(value.getStart(), ("the values of a row are literals, like 7_i32, so a row "
                            + "cannot have '%s'").formatted(named.getText()));
                }
                inLiteralRow = true;
                try {
                    cells.add(expect(value, columns.get(i), "a row of '%s'".formatted(name)).cpp());
                } finally {
                    inLiteralRow = false;
                }
            }
            if (values.size() > columns.size()) {
                throw error(start, "'%s' has rows of %s, but this row has more".formatted(name, types));
            }
            rows.add("{" + String.join(", ", cells) + "}");
            if (isVar) {
                checkArraySize(start, name, rows.size(), columns.size());
            }
        }
        if (context.CUT() != null) {
            throw cut;
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

    // The number of rows of an array, len_col, or of the values of each row, len_row.
    private Expression length(BosonogaParser.ArrayLengthContext context) {
        var name = context.NAME().getSymbol();
        checkNotHidden(name);
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
            array.markCalled();
        }
        var count = context.count.getType() == BosonogaParser.LEN_COL ? array.rowCount() : array.results().size();
        return new Expression("std::int32_t{" + count + "}", Kind.I32, BigInteger.valueOf(count));
    }

    // A value, which can be flags joined to more flags with shift_or.
    private Expression flags(BosonogaParser.ValueInitializerContext context) {
        var first = expression(context.value());
        var flags = context.orFlag();
        // As in the C bosonoga, only flags are followed by shift_or.
        if (!flags.isEmpty() && first.kind() != Kind.FLAGS) {
            throw error(flags.getFirst().SHIFT_OR().getSymbol(), "expected a statement, got 'shift_or'");
        }
        var cpp = new StringBuilder(first.cpp());
        for (var flag : flags) {
            if (flag.value() == null) {
                throw expectedValue(next(flag.SHIFT_OR().getSymbol()));
            }
            cpp.append(" | ").append(expect(flag.value(), Kind.FLAGS, "shift_or").cpp());
        }
        return new Expression(cpp.toString(), first.kind());
    }

    @Override
    public String visitSetStatement(BosonogaParser.SetStatementContext context) {
        var name = context.NAME().getSymbol();
        var shown = rawShown(name);
        checkNotSetting(name);
        checkNotHidden(name);
        if (constants.containsKey(name.getText())) {
            throw error(name, "'%s' is a constant and cannot be set".formatted(shown));
        }
        var found = find(name);
        if (found.isEmpty() && arrays.containsKey(name.getText())) {
            throw error(name, "'%s' is a val array and cannot be set, but a var array of a block can"
                    .formatted(shown));
        }
        var target = found.orElseThrow(() -> unknownVariable(name));
        var setterStart = context.setter() != null ? context.setter().getStart() : context.CUT().getSymbol();
        if (target.array != null) {
            if (!(context.setter() instanceof BosonogaParser.RowSetterContext row)) {
                throw errorAtCut(setterStart, ("'%s' is an array, whose rows are set with set %s at "
                        + "INDEX to and one value per column").formatted(shown, shown));
            }
            return rowWrite(name, target.array, row);
        }
        if (target.isNew && target.kind == Kind.XML_STR && target.freedBy == null) {
            if (target.isVal) {
                throw error(name, "'%s' is a val, and only a var of new xml_str is joined to".formatted(shown));
            }
            if (!(context.setter() instanceof BosonogaParser.JoinSetterContext join)) {
                throw errorAtCut(setterStart, "'%s' is new, so it is only joined to, as in set %s to_join"
                        .formatted(shown, shown));
            }
            // Joining to it is not reading it, as in the C bosonoga.
            var joined = new StringBuilder("runtime::XmlJoin(").append(target.cpp).append(')');
            readingNew(() -> xmlContent(join.value(), next(join.TO_JOIN().getSymbol()), joined));
            return target.cpp + " = " + joined.append(".take()") + ";";
        }
        if (target.isNew) {
            throw error(name, "'%s' is %s and cannot be set, but it can be unset and declared again"
                    .formatted(shown, target.made()));
        }
        if (target.isVal) {
            throw error(name, "'%s' is a val and cannot be set".formatted(shown));
        }
        if (context.CUT() != null) {
            throw cut;
        }
        if (context.setter() instanceof BosonogaParser.RowSetterContext row) {
            throw error(row.AT().getSymbol(), "expected to, to_add, to_subtract, to_multiply, to_divide, "
                    + "to_mod or a conversion, got 'at'");
        }
        var setter = context.setter() instanceof BosonogaParser.AssignSetterContext assign
                ? assign.assigned() : context.setter();
        var assigned = switch (setter) {
            case BosonogaParser.LengthSetterContext length -> {
                var result = length(length.arrayLength());
                if (target.kind != Kind.I32) {
                    throw error(length.getStart(), "'%s' is %s, but a len is i32".formatted(shown, target.kind.shown));
                }
                yield result.cpp();
            }
            case BosonogaParser.CallSetterContext call -> {
                var result = takenResult(name, call.callee, call.arguments(), call.takeOne());
                if (!isCallable(call.callee)) {
                    if (result.kind() != target.kind) {
                        throw error(call.callee, "set ... to needs %s, but '%s' is %s".formatted(target.kind.shown,
                                call.callee.getText(), result.kind().shown));
                    }
                    yield result.cpp();
                }
                if (result.kind() != target.kind) {
                    var called = callable(call.callee);
                    throw error(name, "'%s' is %s, but '%s' %s %s".formatted(shown,
                            target.kind.shown, call.callee.getText(), called.gives(), result.kind().shown));
                }
                yield result.cpp();
            }
            case BosonogaParser.ToSetterContext to when isCalled(to.value()) -> {
                var result = takenResult(name, to.value().getFirst().getStart(), null, null);
                if (result.kind() != target.kind) {
                    var callee = to.value().getFirst().getStart();
                    throw error(name, "'%s' is %s, but '%s' %s %s".formatted(shown,
                            target.kind.shown, callee.getText(), callable(callee).gives(), result.kind().shown));
                }
                yield result.cpp();
            }
            case BosonogaParser.ToSetterContext to -> {
                var values = to.value();
                var result = expect(values.getFirst(), target.kind, "set ... to");
                if (values.size() > 1) {
                    throw error(values.get(1).getStart(),
                            "set ... to sets '%s' to a single value".formatted(shown));
                }
                yield result.cpp();
            }
            case BosonogaParser.AccumulationSetterContext accumulation ->
                    accumulation(name, target.kind, accumulation);
            case BosonogaParser.JoinSetterContext join -> throw error(join.TO_JOIN().getSymbol(),
                    "'%s' is %s, but only a var of new xml_str is joined to".formatted(shown, target.kind.shown));
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
        var written = context.value().subList(1, context.value().size());
        var values = withoutCut(written);
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
        if (values.size() < written.size()) {
            throw cut;
        }
        return "runtime::row(" + where(name) + ", " + array.cpp() + ", \"" + shown + "\", " + index + ") = {"
                + String.join(", ", cells) + "};";
    }

    // As in the C bosonoga, only what new made is unset, in its own block, once it
    // is read.
    @Override
    public String visitUnsetStatement(BosonogaParser.UnsetStatementContext context) {
        var name = context.NAME().getSymbol();
        var shown = rawShown(name);
        checkNotSetting(name);
        checkNotHidden(name);
        var variable = find(name).orElseThrow(() -> unknownVariable(name));
        if (!variable.isNew) {
            throw error(name, "'%s' is not new, and only what new made is unset".formatted(shown));
        }
        if (variable.freedBy != null) {
            throw error(name, "'%s' is freed after the block that %s gives it, and is not unset"
                    .formatted(shown, variable.freedBy));
        }
        if (scopes.getFirst().get(name.getText()) != variable) {
            throw error(name, "'%s' is unset in the block where it is new".formatted(shown));
        }
        if (!variable.isRead) {
            throw neverRead(variable.declaration);
        }
        scopes.getFirst().remove(name.getText());
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
                            .formatted(rawShown(name), kind.shown));
        }
        var values = withoutCut(context.value());
        if (values.isEmpty()) {
            throw expectedValue(context.value().getFirst().getStart());
        }
        var first = expect(values.getFirst(), kind, word).cpp();
        if (context.accumulation.getType() == BosonogaParser.TO_MOD && values.size() < 2) {
            var got = next(values.getFirst().getStop());
            throw error(got, "expected a value to divide by, got %s".formatted(shown(got)));
        }
        var cpp = first + values.stream().skip(1).map(value -> ", " + expect(value, kind, word).cpp())
                .collect(joining());
        if (values.size() < context.value().size()) {
            throw cut;
        }
        var step = word.substring("to_".length());
        return "runtime::accumulate<" + kind.cpp + ">(" + where(name) + ", runtime::Step::" + step
                + ", {" + cpp + "})";
    }

    private Expression operation(BosonogaParser.OperationContext context) {
        var operation = Operation.of(context.function.getText());
        var values = context.value();
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
        if (name != null) {
            // The C loop parser checks its counter name before the block.
            checkNewName(name);
        }
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
            throw leavesNew(jump, owner.get());
        }
        return jump.getText() + ";";
    }

    // What a block is given is freed after the block, which a jump would leave.
    private BosonogaException leavesNew(Token jump, Variable owner) {
        var name = owner.declaration.getText();
        if (owner.freedBy != null) {
            return error(jump, "%s cannot leave the block that %s gives '%s'"
                    .formatted(jump.getText(), owner.freedBy, name));
        }
        return error(jump, "unset '%s' before %s".formatted(name, jump.getText()));
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
            throw leavesNew(start, owner.get());
        }
        if (currentFunction == null && !section.equals("bosonoga_main") && !section.equals("gfx_main")) {
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
        // As in the C bosonoga, each value is checked as it is read.
        var given = withoutCut(values);
        var cpp = new ArrayList<String>();
        for (int i = 0; i < results.size(); i++) {
            if (i == given.size()) {
                throw error(start, "'%s' returns %s, but this return has %d"
                        .formatted(name, valuesShown(results), i));
            }
            cpp.add(expect(given.get(i), results.get(i), "return").cpp());
        }
        if (given.size() > results.size()) {
            throw error(start, "'%s' returns %s, but this return has more"
                    .formatted(name, valuesShown(results)));
        }
        // A token that is not a value ends the return, as in the C bosonoga.
        if (given.size() < values.size()) {
            var got = ((BosonogaParser.CutValueContext) values.get(given.size())).VALUE_CUT().getSymbol();
            throw error(got, isEnd(CutToken.original(got)) ? "expected do, got end of file"
                    : "expected a statement, got " + shown(got));
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
        if (settings.has(text)) {
            throw error(name, "expected a function name, got '%s'".formatted(rawShown(name)));
        }
        if (!isFunctionName(text)) {
            if (text.equals(FUNCTION_SUFFIX)) {
                throw error(name, "the name of a function has more before _fn, as in double_fn");
            }
            // The old fn_ prefix, without it in the suggestion.
            var displayed = rawShown(name);
            var suggested = displayed.startsWith("fn_") && displayed.length() > 3 ? displayed.substring(3) : displayed;
            throw error(name, "the name of a function ends with _fn, as in %s_fn".formatted(suggested));
        }
        if (functions.containsKey(text)) {
            throw error(name, "function '%s' already exists".formatted(rawShown(name)));
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
            if (context.block().CUT() != null) {
                throw cut;
            }
            throw error(context.block().getStart(), ("'%s' must say what it returns: ret and its types, "
                    + "or ret_void for nothing").formatted(rawShown(name)));
        }
        var function = new Function(name, List.copyOf(parameters), results,
                cppFunctionName(text), false, 0);
        functions.put(text, function);
        declaredFunctions.add(function);
        declareHome(name);
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
                throw neverRead(parameter.declaration);
            }
        }
        scopes.addAll(outerScopes);
        return CppText.returnType(results) + " " + cppFunctionName(text) + "(" + cppParameters + ") {\n" + body + "}\n";
    }

    // The type is after an underscore, as in count_i32, and the longest type is
    // taken, as in the C bosonoga.
    private Kind parameterKind(Token parameter) {
        var text = parameter.getText();
        for (int start = 2; start < text.length(); start++) {
            if (text.charAt(start - 1) != '_') {
                continue;
            }
            var kind = Kind.named(text.substring(start));
            if (kind.isPresent()) {
                return kind.get();
            }
        }
        throw error(parameter, "expected a parameter whose name ends with its type, like count_i32, got '%s'"
                .formatted(rawShown(parameter)));
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
        return Kind.named(matcher.group(2)).map(kind -> new Indexed(Integer.parseInt(matcher.group(1)), kind));
    }

    @Override
    public String visitCallStatement(BosonogaParser.CallStatementContext context) {
        var function = callable(context.callee);
        var name = rawShown(context.callee);
        var call = call(function, context.callee, context.arguments());
        var results = function.results();
        if (results.isEmpty()) {
            var following = next(context.getStop());
            if (context.taking() != null || CutToken.original(following).getType() == BosonogaParser.DI) {
                throw error(context.taking() != null ? context.taking().WITH().getSymbol() : following,
                        "'%s' returns no values, so its call has neither with ret nor a block".formatted(name));
            }
            return call + ";";
        }
        if (context.taking() == null) {
            throw needsBlock(context.CALL().getSymbol(), function);
        }
        // Copied out first, as in the C bosonoga.
        var result = "result_" + ++resultCount;
        var declared = new ArrayList<Declared>();
        var bindings = bindings(context.taking(), "'" + name + "'", function.gives(), results, function);
        var taken = taken(name, function.gives(), results, Map.of(), bindings);
        for (int i = 0; i < taken.size(); i++) {
            var each = taken.get(i);
            // A row is a tuple even of one value, and a function of one value is not.
            var value = !function.isArray() && results.size() == 1
                    ? result
                    : "std::get<" + each.index() + ">(" + result + ")";
            declared.add(new Declared(bindings.get(i).NAME().getSymbol(), each.kind(),
                    cpp -> each.kind().cpp + " " + cpp + " = " + value + ";"));
        }
        var block = block(takingBlock(context.taking()), List.of("const auto " + result + " = " + call + ";"),
                declared);
        return "{\n" + block + INDENT.repeat(depth) + "}";
    }

    private BosonogaException expectedRet(Token with) {
        var got = next(with);
        return error(got, "expected ret, got %s".formatted(shown(got)));
    }

    // The bindings of with ret, of a program that may be cut after with or ret.
    private List<BosonogaParser.BindingContext> bindings(BosonogaParser.TakingContext taking, String name,
                                                         String gives, List<Kind> results, Function called) {
        if (taking.RET() == null) {
            throw expectedRet(taking.WITH().getSymbol());
        }
        if (taking.binding().isEmpty()) {
            var got = taking.CUT() != null ? taking.CUT().getSymbol() : next(taking.RET().getSymbol());
            if (called != null) {
                throw errorAtCut(got, needsBlockMessage(called));
            }
            throw errorAtCut(got, ("%s %s %s, which its with ret takes for its block, each "
                    + "named, as in with ret 0_%s as value").formatted(name, gives, kindsNamed(results),
                    results.getFirst().shown));
        }
        return taking.binding();
    }

    // As the C bosonoga reads the values to take until di.
    private BosonogaParser.BlockContext takingBlock(BosonogaParser.TakingContext taking) {
        if (taking.block() != null && taking.block().CUT() != null
                && CutToken.original(taking.block().CUT().getSymbol()).getType() == BosonogaParser.DI) {
            // The opener was reached. Let checks of the completed result
            // bindings run before block() reports an error inside the body.
            return taking.block();
        }
        if (taking.block() == null || taking.block().CUT() != null) {
            var got = next(taking.binding().getLast().getStop());
            throw error(got, "expected another value to take, like 1_i32 as count, or di, got %s"
                    .formatted(shown(got)));
        }
        return taking.block();
    }

    // The values that with ret takes, each once by its index and type, by
    // increasing index. A value can be taken as another type that also lists.
    private List<Indexed> taken(String name, String gives, List<Kind> results, Map<Integer, Set<Kind>> also,
                                List<BosonogaParser.BindingContext> bindings) {
        var taken = new ArrayList<Indexed>();
        var names = new HashSet<String>();
        Indexed previous = null;
        for (var binding : bindings) {
            var position = binding.position;
            var shownPosition = position.getText();
            var each = indexed(position)
                    .filter(indexed -> indexed.index() < results.size()
                            && (indexed.kind() == results.get(indexed.index())
                            || also.getOrDefault(indexed.index(), Set.of()).contains(indexed.kind())))
                    .orElseThrow(() -> error(position, "'%s' %s %s, so it has no %s"
                            .formatted(name, gives, kindsNamed(results), shownPosition)));
            if (previous != null && each.index() <= previous.index()) {
                throw error(position, ("the values are taken once each, by increasing index, so %s cannot "
                        + "follow %d_%s").formatted(shownPosition, previous.index(), previous.kind().shown));
            }
            previous = each;
            taken.add(each);
            if (binding.AS() == null) {
                var got = CutToken.original(binding.CUT() != null ? binding.CUT().getSymbol() : next(position));
                throw errorAtCut(got, "expected as and a name after %s, as in %s as value, got '%s'".formatted(
                        shownPosition, shownPosition, isEnd(got) ? "end of file" : got.getText()));
            }
            if (binding.NAME() == null) {
                var got = next(binding.AS().getSymbol());
                throw error(got, "expected a variable name, got %s".formatted(shown(got)));
            }
            // The C parser declares each result as it reads its binding, so
            // name errors precede any error in the next binding. Check here
            // without exposing the variables before the generated block.
            var bound = binding.NAME().getSymbol();
            checkNewName(bound);
            if (!names.add(bound.getText())) {
                throw error(bound, "variable '%s' already exists".formatted(rawShown(bound)));
            }
        }
        return taken;
    }

    // As in the C bosonoga, io_read, io_write, io_modified, xml_child,
    // xml_attribute and str_parse give their values to a block, and what they
    // give as text is freed after it.
    @Override
    public String visitGivenStatement(BosonogaParser.GivenStatementContext context) {
        var start = context.given;
        var word = start.getText();
        var values = context.value();
        var operands = new ArrayList<String>();
        var gives = readingNew(() -> gives(start, values, operands));
        var results = gives.results();
        if (values.size() > operands.size() || context.taking() == null) {
            var got = values.size() > operands.size()
                    ? values.get(operands.size()).getStart()
                    : after(values, operands.size(), start);
            throw errorAtCut(got, ("%s gives %s, which with ret takes for a block, each named, as in with ret "
                    + "0_bool as done").formatted(word, kindsNamed(results)));
        }
        var bindings = bindings(context.taking(), "'" + word + "'", "gives", results, null);
        var taken = taken(word, "gives", results, gives.also(), bindings);
        var takenBlock = takingBlock(context.taking());
        var text = taken.stream().filter(each -> each.index() == 1).findFirst();
        String call;
        if (start.getType() == BosonogaParser.STR_PARSE) {
            var number = text.orElseThrow(() -> error(start, "str_parse takes its number, whose type it parses, "
                    + "as in with ret 0_bool as parsed 1_i32 as number"));
            call = "runtime::str_parse<" + number.kind().cpp + ">(" + operands.getFirst() + ")";
        } else {
            // As in the C bosonoga, io_read checks only a text that the block takes.
            if (start.getType() == BosonogaParser.IO_READ) {
                operands.add(String.valueOf(text.isPresent()));
                operands.add(String.valueOf(text.isPresent() && text.get().kind() == Kind.XML_STR));
            } else if (start.getType() == BosonogaParser.IO_WRITE) {
                operands.add(String.valueOf(gives.writesXml()));
            }
            call = "runtime::" + word + "(" + where(start) + ", " + String.join(", ", operands) + ")";
        }
        var result = "result_" + ++resultCount;
        var declared = new ArrayList<Declared>();
        for (int i = 0; i < taken.size(); i++) {
            var each = taken.get(i);
            var isFreed = gives.freed().contains(each.index());
            var value = "std::get<" + each.index() + ">(" + result + ")";
            declared.add(new Declared(bindings.get(i).NAME().getSymbol(), each.kind(), isFreed ? word : null,
                    cpp -> (isFreed ? "const " : "") + each.kind().cpp + " " + cpp + " = " + value + ";"));
        }
        var block = block(takenBlock, List.of("const auto " + result + " = " + call + ";"),
                declared);
        return "{\n" + block + INDENT.repeat(depth) + "}";
    }

    // Adds the operands of a given statement, each checked in order.
    private Gives gives(Token start, List<BosonogaParser.ValueContext> values, List<String> operands) {
        switch (start.getType()) {
            case BosonogaParser.STR_PARSE -> {
                operands.add(givenOperand(values, 0, Kind.STR, start).cpp());
                return new Gives(List.of(Kind.BOOL, Kind.I32), Map.of(1, Set.of(Kind.I64, Kind.U32, Kind.U64)),
                        Set.of(), false);
            }
            case BosonogaParser.XML_CHILD, BosonogaParser.XML_ATTRIBUTE -> {
                var isChild = start.getType() == BosonogaParser.XML_CHILD;
                operands.add(givenOperand(values, 0, Kind.XML_STR, start).cpp());
                operands.add("\"" + xmlName(values, 1, isChild ? "an element" : "an attribute",
                        after(values, 1, start)) + "\"");
                if (isChild) {
                    operands.add(givenOperand(values, 2, Kind.I32, start).cpp());
                }
                return new Gives(List.of(Kind.BOOL, isChild ? Kind.XML_STR : Kind.STR), Map.of(), Set.of(1), false);
            }
            default -> {
                operands.add(givenOperand(values, 0, Kind.DIR, start).cpp());
                operands.add(givenOperand(values, 1, Kind.STR, start).cpp());
                if (start.getType() == BosonogaParser.IO_READ) {
                    return new Gives(List.of(Kind.BOOL, Kind.XML_STR, Kind.STR), Map.of(1, Set.of(Kind.STR)),
                            Set.of(1, 2), false);
                }
                if (start.getType() == BosonogaParser.IO_WRITE) {
                    if (values.size() < 3) {
                        throw expectedValue(after(values, 2, start));
                    }
                    if (values.get(2) instanceof BosonogaParser.CutValueContext) {
                        throw cut;
                    }
                    var written = expression(values.get(2));
                    if (!written.kind().isText()) {
                        throw error(values.get(2).getStart(), "io_write writes a str or an xml_str, but '%s' is %s"
                                .formatted(values.get(2).getText(), written.kind().shown));
                    }
                    operands.add(written.cpp());
                    return new Gives(List.of(Kind.BOOL, Kind.BOOL, Kind.STR), Map.of(), Set.of(2),
                            written.kind() == Kind.XML_STR);
                }
                return new Gives(List.of(Kind.BOOL, Kind.I64), Map.of(), Set.of(), false);
            }
        }
    }

    private Expression givenOperand(List<BosonogaParser.ValueContext> values, int index, Kind kind, Token start) {
        if (index >= values.size()) {
            throw expectedValue(after(values, index, start));
        }
        if (values.get(index) instanceof BosonogaParser.CutValueContext cutValue) {
            throw expectedValue(cutValue.VALUE_CUT().getSymbol());
        }
        return expect(values.get(index), kind, start.getText());
    }

    // The one value of a call that var and set ... to take, as with ret 0_i32.
    // As the C bosonoga, a name of a function or an array after is or to is a
    // call, and any other is a value.
    private boolean isCalled(List<BosonogaParser.ValueContext> values) {
        return values.getFirst() instanceof BosonogaParser.NameValueContext named
                && isCallable(named.NAME().getSymbol());
    }

    private boolean isCallable(Token name) {
        return find(name).filter(found -> found.array != null).isPresent() || functions.containsKey(name.getText())
                || arrays.containsKey(name.getText());
    }

    // Take is what follows the call, or null when nothing of with ret does.
    private Expression takenResult(Token target, Token callee, BosonogaParser.ArgumentsContext arguments,
                                   BosonogaParser.TakeOneContext take) {
        // A name alone after is or to, of a variable or a constant.
        if (!isCallable(callee)) {
            var value = read(callee);
            if (arguments != null) {
                var got = arguments.getStart();
                throw error(got, "expected a statement, got %s".formatted(shown(got)));
            }
            if (take != null) {
                if (take.WITH() == null) {
                    throw cut;
                }
                throw error(take.WITH().getSymbol(), "expected a statement, got 'with'");
            }
            return value;
        }
        var function = callable(callee);
        var name = rawShown(callee);
        var call = call(function, callee, arguments);
        var results = function.results();
        if (results.isEmpty()) {
            throw error(target, "'%s' returns no values, so it is called with call".formatted(name));
        }
        if (results.size() > 1) {
            throw needsBlock(target, function);
        }
        var kind = results.getFirst();
        if (take == null || take.WITH() == null) {
            var got = take != null ? take.CUT().getSymbol()
                    : arguments != null && !arguments.value().isEmpty()
                    && arguments.value().getLast() instanceof BosonogaParser.CutValueContext cutValue
                    ? cutValue.VALUE_CUT().getSymbol()
                    : next(arguments != null ? arguments.getStop() : callee);
            throw errorAtCut(got, "expected with ret 0_%s, the value of '%s' that is taken, got '%s'"
                    .formatted(kind.shown, name, rawShown(got)));
        }
        if (take.RET() == null) {
            throw expectedRet(take.WITH().getSymbol());
        }
        var result = take.result != null ? take.result : take.CUT() != null ? take.CUT().getSymbol()
                : next(take.RET().getSymbol());
        var taken = take.result != null ? indexed(result) : Optional.<Indexed>empty();
        if (taken.isEmpty() || taken.get().index() != 0 || taken.get().kind() != kind) {
            throw errorAtCut(result, "'%s' %s %s, so its call ends with ret 0_%s, not '%s'"
                    .formatted(name, function.gives(), kind.shown, kind.shown, rawShown(result)));
        }
        return new Expression(function.isArray() ? "std::get<0>(" + call + ")" : call, kind);
    }

    private BosonogaException needsBlock(Token token, Function function) {
        return error(token, needsBlockMessage(function));
    }

    private static String needsBlockMessage(Function function) {
        var results = function.results();
        return ("'%s' %s %s, which %scall ... with ret takes for its block, each named, "
                + "as in with ret 0_%s as value").formatted(rawShown(function.name()), function.gives(),
                kindsNamed(results), results.size() == 1 ? "var, set ... to or " : "",
                results.getFirst().shown);
    }

    // A function that only calls itself is not used, as in the C bosonoga.
    private Function callable(Token callee) {
        checkNotHidden(callee);
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
            throw error(callee, "unknown function '%s'".formatted(rawShown(callee)));
        }
        if (function != currentFunction) {
            function.markCalled();
        }
        return function;
    }

    private String call(Function function, Token callee, BosonogaParser.ArgumentsContext arguments) {
        var name = rawShown(callee);
        var values = arguments == null ? List.<BosonogaParser.ValueContext>of() : withoutCut(arguments.value());
        if (function.isArray()) {
            if (arguments == null || arguments.AT() == null) {
                throw error(arguments == null ? next(callee) : arguments.getStart(), ("'%s' is an array, "
                        + "which is read at one i32 index, as in %s at 0_i32").formatted(name, name));
            }
            if (values.isEmpty()) {
                throw cutOr(arguments.value(), next(arguments.AT().getSymbol()));
            }
            var index = expect(values.getFirst(), Kind.I32, "the index of '%s'".formatted(name)).cpp();
            if (values.size() > 1) {
                throw error(values.get(1).getStart(), ("'%s' is an array, which is read at one i32 index, "
                        + "but is given more").formatted(name));
            }
            return "runtime::row(" + where(callee) + ", " + function.cpp() + ", \"" + callee.getText() + "\", "
                    + index + ")";
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
        if (parameters.isEmpty() && arguments == null && isValueStart(next(callee))) {
            throw error(next(callee), "'%s' takes no values, but is given more".formatted(name));
        }
        if (!parameters.isEmpty() && of == null) {
            throw error(next(callee), "'%s' takes %s, which follow of, as in %s of ..."
                    .formatted(name, shownParameters, name));
        }
        var cpp = new StringBuilder(where(callee));
        for (int i = 0; i < parameters.size(); i++) {
            if (i == values.size()) {
                throw error(next(values.isEmpty() ? of.getSymbol() : values.getLast().getStop()),
                        "'%s' takes %s, but is given %d".formatted(name, shownParameters, i));
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
        var left = readingNew(() -> expression(context.value(0)));
        var right = context.compare != null ? readingNew(() -> expression(context.value(1))) : null;
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

    // The values before the CUT of a program cut among them, which the C
    // bosonoga reads before it finds the syntax error.
    private static List<BosonogaParser.ValueContext> withoutCut(List<BosonogaParser.ValueContext> values) {
        return values.stream().takeWhile(value -> !(value instanceof BosonogaParser.CutValueContext)).toList();
    }

    // A value that is missing, where the program may be cut.
    private BosonogaException cutOr(List<BosonogaParser.ValueContext> values, Token missing) {
        if (!values.isEmpty() && values.getFirst() instanceof BosonogaParser.CutValueContext) {
            return cut;
        }
        return expectedValue(missing);
    }

    // As the C bosonoga sees that a value starts at the token.
    private static boolean isValueStart(Token token) {
        var type = CutToken.original(token).getType();
        return type == BosonogaParser.NUMBER || type == BosonogaParser.STRING || type == BosonogaParser.NAME
                || type == BosonogaParser.GFX_DELTA_TIME || type == BosonogaParser.GFX_KEY
                || type == BosonogaParser.TRUE_BOOL || type == BosonogaParser.FALSE_BOOL
                || type == BosonogaParser.SHIFT_BIT || type == BosonogaParser.RESULT_NAME;
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
        // A str and an xml_str compare by their text.
        if (kind != right.kind() && !(kind.isText() && right.kind().isText())) {
            throw error(compare, "cannot compare %s with %s".formatted(kind.shown, right.kind().shown));
        }
        if (kind == Kind.FLAGS) {
            throw error(compare, "flags can only be compared with shift_has");
        }
        if (kind == Kind.COLOR) {
            throw error(compare, "colors cannot be compared");
        }
        if ((kind == Kind.BOOL || kind.isText() || kind.isNamed())
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
            case BosonogaParser.GfxValueContext special -> gfxValue(special.special);
            case BosonogaParser.NumberValueContext number -> number(number.NUMBER().getSymbol());
            case BosonogaParser.ShiftValueContext shift -> new Expression(
                    "std::uint32_t{1U << " + parseShiftBit(shift.SHIFT_BIT().getSymbol()) + "}",
                    Kind.FLAGS);
            case BosonogaParser.BoolValueContext bool ->
                    new Expression(bool.TRUE_BOOL() != null ? "true" : "false", Kind.BOOL);
            case BosonogaParser.StringValueContext string -> string(string.STRING().getSymbol());
            case BosonogaParser.NameValueContext name -> read(name.NAME().getSymbol());
            case BosonogaParser.ResultValueContext result -> throw error(result.getStart(),
                    "%s is not a value, a call names the values that it takes, as in call ... with ret 0_i32 as count"
                            .formatted(result.getText()));
            case BosonogaParser.CutValueContext ignored -> throw cut;
            default -> throw new IllegalStateException("no C++ for the value " + value.getText());
        };
    }

    private Expression gfxValue(Token token) {
        var delta = token.getType() == BosonogaParser.GFX_DELTA_TIME;
        var required = delta ? "gfx_frame" : "gfx_key_down";
        if (currentFunction != null || inLiteralRow || !section.equals(required)) {
            throw error(token, token.getText() + " can only be in " + required);
        }
        return new Expression("gfx_context.scene." + (delta ? "delta_time" : "key"), delta ? Kind.F32 : Kind.INPUT_KEY);
    }

    // As in the C bosonoga, a str has no escape sequences, so it is the text
    // between its quotes, and the C++ escapes only what C++ needs.
    private Expression string(Token literal) {
        var kind = StringLiteral.check(literal);
        var text = literal.getText();
        return new Expression(CppText.stdString(text.substring(1, text.lastIndexOf('"'))), kind);
    }

    private Expression expect(BosonogaParser.ValueContext value, Kind kind, String context) {
        var expression = expression(value);
        if (expression.kind() != kind) {
            throw error(value.getStart(), "%s needs %s, but '%s' is %s"
                    .formatted(context, kind.shown, rawShown(value.getStart()), expression.kind().shown));
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
        return declare(name, kind, isVal, isNew, null);
    }

    private Variable declare(Token name, Kind kind, boolean isVal, boolean isNew, String freedBy) {
        checkNewName(name);
        var text = name.getText();
        var count = declarations.merge(text, 1, Integer::sum);
        // The prefix keeps a bosonoga name from being a C++ keyword, such as new.
        var cpp = count == 1 ? "v_" + text : "v" + count + "_" + text;
        var variable = new Variable(name, kind, isVal, isNew, loopDepth, cpp, freedBy, modules.here(isPubVariable));
        isPubVariable = false;
        scopes.getFirst().put(text, variable);
        return variable;
    }

    private void checkNewName(Token name) {
        if (name.getType() == BosonogaParser.RESULT_NAME) {
            throw error(name, "expected a variable name, got %s".formatted(shown(name)));
        }
        checkNotSetting(name);
        if (constants.containsKey(name.getText())) {
            throw error(name, "a constant with this name already exists");
        }
        if (findAny(name).isPresent()) {
            throw error(name, "variable '%s' already exists".formatted(rawShown(name)));
        }
        if (isFunctionName(name.getText())) {
            throw error(name, "'%s' ends with _fn, which only the name of a function does"
                    .formatted(rawShown(name)));
        }
        if (functions.containsKey(name.getText())) {
            throw error(name, "'%s' is already a function".formatted(rawShown(name)));
        }
        if (arrays.containsKey(name.getText())) {
            throw error(name, "'%s' is already an array".formatted(rawShown(name)));
        }
    }

    // A variable that is seen here.
    private Optional<Variable> find(Token name) {
        return findAny(name).filter(found -> modules.isVisible(found.home));
    }

    private Optional<Variable> findAny(Token name) {
        for (var scope : scopes) {
            var variable = scope.get(name.getText());
            if (variable != null) {
                return Optional.of(variable);
            }
        }
        return Optional.empty();
    }

    private Expression read(Token name) {
        checkNotHidden(name);
        var constant = constants.get(name.getText());
        if (constant != null) {
            constant.isUsed = true;
            return constant.value;
        }
        if (settings.has(name.getText())) {
            return settings.value(name.getText());
        }
        if (functions.containsKey(name.getText())) {
            throw error(name, "'%s' is a function, which is called with var, set ... to or call"
                    .formatted(rawShown(name)));
        }
        var variable = find(name);
        if (arrays.containsKey(name.getText()) || variable.filter(found -> found.array != null).isPresent()) {
            throw error(name, "'%s' is an array, which is read with var, set ... to or call"
                    .formatted(rawShown(name)));
        }
        var found = variable.orElseThrow(() -> unknownVariable(name));
        // A copy would outlive the unset of what it points into, as in the C bosonoga.
        if (found.isNew && !readsNew) {
            throw error(name, "'%s' is %s, so it %s".formatted(name.getText(), found.made(), NEW_READS));
        }
        found.isRead = true;
        return new Expression(found.cpp, found.kind);
    }

    private void checkNotSetting(Token name) {
        if (settings.has(name.getText())) {
            throw error(name, "expected a variable name, got '%s'".formatted(rawShown(name)));
        }
    }

    private BosonogaException unknownVariable(Token name) {
        if (functions.containsKey(name.getText()) || arrays.containsKey(name.getText())) {
            var isArray = arrays.containsKey(name.getText());
            return error(name, "'%s' is %s, which is %s with var, set ... to or call"
                    .formatted(rawShown(name), isArray ? "an array" : "a function", isArray ? "read" : "called"));
        }
        return error(name, "unknown %s '%s'".formatted(isFunctionName(name.getText()) ? "function" : "variable",
                rawShown(name)));
    }

    private String where(Token token) {
        return locations.where(token);
    }

    private void declareHome(Token name) {
        modules.declare(name.getText(), isPub);
    }

    // As in the C bosonoga, a name of another scope that is not seen from here
    // says why.
    private void checkNotHidden(Token name) {
        var variable = findAny(name);
        var home = variable.isPresent() ? variable.get().home : modules.home(name.getText());
        var hidden = modules.hidden(home, rawShown(name));
        if (hidden.isPresent()) {
            throw error(name, hidden.get());
        }
    }

    private BosonogaException neverRead(Token declaration) {
        return error(declaration, "'%s' is never read".formatted(rawShown(declaration)));
    }

    private BosonogaException error(Token token, String message) {
        return diagnostics.error(token, message);
    }

    private BosonogaException errorAtCut(Token token, String message) {
        return diagnostics.errorAtCut(token, message);
    }
}
