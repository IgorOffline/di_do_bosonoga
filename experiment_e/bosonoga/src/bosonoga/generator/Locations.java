package bosonoga.generator;

import java.util.LinkedHashMap;
import java.util.Map;
import org.antlr.v4.runtime.Token;

// Where a runtime error is reported, as runtime::Where, by file, line and byte
// column. The program's own file is source, and each file it reads that a
// runtime error can name is source_1, source_2... in the order first named.
final class Locations {
    private final String programPath;
    private final Map<String, String> names = new LinkedHashMap<>();

    Locations(String programPath) {
        this.programPath = programPath;
    }

    // A synthetic token without a stream is of the program.
    String path(Token token) {
        return token.getInputStream() != null ? token.getInputStream().getSourceName() : programPath;
    }

    String where(Token token) {
        var path = path(token);
        var cpp = names.get(path);
        if (cpp == null) {
            var imported = names.keySet().stream().filter(file -> !file.equals(programPath)).count();
            cpp = path.equals(programPath) ? "source" : "source_" + (imported + 1);
            names.put(path, cpp);
        }
        return "{" + cpp + ", " + token.getLine() + ", " + Columns.of(token) + "}";
    }

    // The C++ constants of the files that where named.
    String declarations() {
        var cpp = new StringBuilder();
        for (var file : names.entrySet()) {
            cpp.append("constexpr const char *").append(file.getValue()).append(" = \"")
                    .append(CppText.escapedBytes(file.getKey())).append("\";\n");
        }
        if (!names.isEmpty()) {
            cpp.append('\n');
        }
        return cpp.toString();
    }
}
