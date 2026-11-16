package bosonoga.generator;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import org.antlr.v4.runtime.Token;

// As the C bosonoga's test runner, every file is scanned for imports before
// the programs are selected, so an imported file needs no special suffix.
final class TestSources {
    private TestSources() {
    }

    static List<Path> programs(List<Path> candidates) {
        var imported = new HashSet<Path>();
        for (var path : candidates) {
            imports(path, imported);
        }
        return candidates.stream().filter(path -> !path.toString().endsWith(ProgramName.MODULE_SUFFIX)
                && !imported.contains(path.toAbsolutePath().normalize())).toList();
    }

    private static void imports(Path path, Set<Path> imported) {
        try {
            var lexer = Tokens.lexer(Files.readString(path), path.toString());
            while (true) {
                var token = checked(lexer);
                if (token.getType() == Token.EOF) {
                    return;
                }
                if (token.getType() == BosonogaLexer.MOD || token.getType() == BosonogaLexer.IO_RAW_IMPORT) {
                    var name = checked(lexer);
                    if (name.getType() == BosonogaLexer.STRING && StringLiteral.check(name) == Kind.STR) {
                        var text = name.getText();
                        var file = text.substring(1, text.lastIndexOf('"'));
                        // Only a name in the same directory is matched.
                        if (!file.isEmpty() && file.indexOf('/') < 0 && file.indexOf('\\') < 0) {
                            imported.add(path.toAbsolutePath().getParent().resolve(file).normalize());
                        }
                    }
                }
            }
        } catch (IOException | BosonogaException ignored) {
            // Best effort: generating the program reports its errors in order.
        }
    }

    private static Token checked(BosonogaLexer lexer) {
        var token = lexer.nextToken();
        Tokens.checkLiteral(token);
        return token;
    }
}
