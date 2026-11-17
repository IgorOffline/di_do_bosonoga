package bosonoga.generator;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AccessDeniedException;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.antlr.v4.runtime.CharStream;
import org.antlr.v4.runtime.Token;
import org.antlr.v4.runtime.TokenFactory;
import org.antlr.v4.runtime.TokenSource;

// The tokens of the program, with those of each file that mod or io_raw_import
// reads after its name. Modules have FILE_START/FILE_END boundaries; raw
// imports have no closing boundary, as the C tokenizer resumes the reader
// even inside a declaration or block. A file that is not read has its
// error kept, for the generator to throw where the program has it.
final class ImportingTokenSource implements TokenSource {
    private final Deque<BosonogaLexer> lexers = new ArrayDeque<>();
    private final Deque<Boolean> modules = new ArrayDeque<>();
    // The first token of a file, read to see that a module has an item.
    private Token first;
    private final Deque<Token> pending = new ArrayDeque<>();
    private final String programPath;
    // The paths of the files read, as their errors name them.
    private final List<String> imported = new ArrayList<>();
    private final Map<Token, BosonogaException> failures = new HashMap<>();
    private int depth;
    // How many tokens are given, so the index of the next one.
    private int given;
    private int previousType = Token.INVALID_TYPE;

    ImportingTokenSource(BosonogaLexer lexer, String programPath) {
        lexers.push(lexer);
        modules.push(false);
        this.programPath = programPath;
    }

    // Why the file after the keyword is not read, if the reason is not its
    // name token itself.
    BosonogaException failure(Token keyword) {
        return failures.get(keyword);
    }

    // An error of the lexer, or of a literal, is at the token that is next.
    @Override
    public Token nextToken() {
        try {
            var token = next();
            given++;
            return token;
        } catch (SyntaxFailure failure) {
            throw failure;
        } catch (BosonogaException exception) {
            throw new SyntaxFailure(exception.getMessage(), given, null, false);
        }
    }

    private Token next() {
        if (!pending.isEmpty()) {
            return track(pending.poll());
        }
        var token = fileToken();
        var type = token.getType();
        // As in the C bosonoga, only an item of the program reads a file.
        if ((type == BosonogaLexer.MOD || type == BosonogaLexer.IO_RAW_IMPORT) && depth == 0
                && previousType != BosonogaLexer.IS && failures.isEmpty()) {
            // The filename can be in the reader when a raw import ends just
            // after this keyword. Do not read an exhausted lexer directly.
            var name = fileToken();
            if (name.getType() != Token.EOF) {
                pending.add(name);
                read(token, name);
            }
        }
        return track(token);
    }

    // One token, across the end of a raw import but not of a module.
    private Token fileToken() {
        while (true) {
            var lexer = lexers.peek();
            var token = first != null ? first : lexer.nextToken();
            first = null;
            if (token.getType() == Token.EOF && lexers.size() > 1) {
                lexers.pop();
                if (!modules.pop()) {
                    // As the C tokenizer resumes its reader, even inside an item or a block.
                    continue;
                }
                return Tokens.synthetic(BosonogaParser.FILE_END, token, "<EOF>");
            }
            return token;
        }
    }

    private Token track(Token token) {
        Tokens.checkLiteral(token);
        if (token.getType() == BosonogaLexer.DI) {
            depth++;
        } else if (token.getType() == BosonogaLexer.DO) {
            depth--;
        } else if (token.getType() == BosonogaParser.FILE_START || token.getType() == BosonogaParser.FILE_END) {
            depth = 0;
        }
        previousType = token.getType();
        return token;
    }

    private void read(Token keyword, Token name) {
        var word = keyword.getText();
        var text = name.getText();
        var suffix = "\"" + "_str";
        if (name.getType() != BosonogaLexer.STRING || !text.endsWith(suffix)
                || text.indexOf('\\') >= 0 || text.indexOf('\0') >= 0) {
            return;
        }
        var fileName = text.substring(1, text.length() - suffix.length());
        if (!isImportName(fileName)) {
            fail(keyword, name, ("%s takes the name of a .bosonoga file in the directory of the program, as in %s "
                    + "\"lorem.bosonoga\"_str").formatted(word, word));
            return;
        }
        if (fileName.equals(basename(programPath))) {
            fail(keyword, name, "a program cannot read itself with %s".formatted(word));
            return;
        }
        if (imported.stream().anyMatch(path -> basename(path).equals(fileName))) {
            fail(keyword, name, "%s is already read, with mod or io_raw_import".formatted(fileName));
            return;
        }
        var directory = dirname(programPath);
        var path = directory.equals(".") ? fileName : Settings.buildFilename(directory, fileName);
        String source;
        try {
            source = new String(Files.readAllBytes(Path.of(path)), StandardCharsets.UTF_8);
        } catch (IOException exception) {
            fail(keyword, name, "cannot read %s: %s".formatted(path, readProblem(path, exception)));
            return;
        }
        imported.add(path);
        var lexer = Tokens.lexer(source, path);
        var start = lexer.nextToken();
        if (keyword.getType() == BosonogaLexer.MOD && start.getType() == Token.EOF) {
            imported.removeLast();
            fail(keyword, name, "%s has no items, and a module has at least one".formatted(fileName));
            return;
        }
        var isModule = keyword.getType() == BosonogaLexer.MOD;
        pending.add(Tokens.synthetic(isModule ? BosonogaParser.FILE_START : BosonogaParser.RAW_START, name, ""));
        lexers.push(lexer);
        modules.push(isModule);
        first = start;
    }

    private void fail(Token keyword, Token name, String message) {
        failures.put(keyword, BosonogaException.at(name, message));
    }

    // As g_file_get_contents says why it cannot read a file.
    private static String readProblem(String path, IOException exception) {
        if (exception instanceof NoSuchFileException) {
            return "Failed to open file “%s”: No such file or directory".formatted(path);
        }
        if (exception instanceof AccessDeniedException) {
            return "Failed to open file “%s”: Permission denied".formatted(path);
        }
        if (Files.isDirectory(Path.of(path))) {
            return "Error reading file “%s”: Is a directory".formatted(path);
        }
        return "Failed to open file “%s”: %s".formatted(path, exception.getMessage());
    }

    static boolean isImportName(String text) {
        return text.length() > ProgramName.SUFFIX.length() && text.charAt(0) != '.'
                && text.endsWith(ProgramName.SUFFIX) && !text.endsWith(ProgramName.EXPECTED_SUFFIX)
                && text.indexOf('/') < 0 && text.indexOf('\\') < 0;
    }

    // As g_path_get_basename and g_path_get_dirname, of a path that names a file.
    static String basename(String path) {
        return path.substring(path.lastIndexOf('/') + 1);
    }

    static String dirname(String path) {
        var end = path.lastIndexOf('/');
        if (end < 0) {
            return ".";
        }
        while (end > 0 && path.charAt(end - 1) == '/') {
            end--;
        }
        return end == 0 ? "/" : path.substring(0, end);
    }

    @Override
    public int getLine() {
        return lexers.peek().getLine();
    }

    @Override
    public int getCharPositionInLine() {
        return lexers.peek().getCharPositionInLine();
    }

    @Override
    public CharStream getInputStream() {
        return lexers.peek().getInputStream();
    }

    @Override
    public String getSourceName() {
        return programPath;
    }

    @Override
    public void setTokenFactory(TokenFactory<?> factory) {
        lexers.forEach(lexer -> lexer.setTokenFactory(factory));
    }

    @Override
    public TokenFactory<?> getTokenFactory() {
        return lexers.peek().getTokenFactory();
    }
}
