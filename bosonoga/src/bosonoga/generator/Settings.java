package bosonoga.generator;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AccessDeniedException;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;
import org.antlr.v4.runtime.CharStreams;
import org.antlr.v4.runtime.Token;

// What regina.ini says, read as the C bosonoga reads it, before the program is
// generated: its colors and globals are then literals of the program.
final class Settings {
    static final String FILE = "regina.ini";

    private static final String GROUP_META = "meta";
    private static final String GROUP_GFX = "gfx";
    private static final String GROUP_COLOR = "color";
    private static final String GROUP_GLOBAL = "global";
    private static final String GROUP_KEY = "key";
    private static final String GROUP_FONT = "font";
    private static final String GROUP_ICON = "icon";
    private static final String GROUP_DIR = "dir";
    private static final String KEY_BACKGROUND = "background";
    private static final int COLOR_HEX_LENGTH = 6;

    // A file that the program names, whose path is from the directory of regina.ini.
    record Asset(String text, String path) {
    }

    final String name;
    final String version;
    // Zero without a group gfx.
    final int gfxWidth;
    final int gfxHeight;
    final int backgroundColor;
    final Map<String, Integer> colors;
    final Map<String, Expression> globals;
    // The name of the sokol_app key of each key.
    final Map<String, String> keys;
    final Map<String, Asset> fonts;
    final Map<String, Asset> icons;
    final Map<String, Asset> dirs;

    private Settings(KeyFile file, String shown, Path directory) {
        name = text(file, shown, GROUP_META, "name");
        version = text(file, shown, GROUP_META, "version");
        var hasGfx = file.hasGroup(GROUP_GFX);
        gfxWidth = hasGfx ? positive(file, shown, "width") : 0;
        gfxHeight = hasGfx ? positive(file, shown, "height") : 0;
        colors = colors(file, shown);
        // As in the C bosonoga, the background is needed only with both groups.
        if (hasGfx && file.hasGroup(GROUP_COLOR) && !colors.containsKey(KEY_BACKGROUND)) {
            throw new BosonogaException("%s: the group %s needs the key %s"
                    .formatted(shown, GROUP_COLOR, KEY_BACKGROUND));
        }
        backgroundColor = colors.getOrDefault(KEY_BACKGROUND, 0);
        globals = globals(file, shown);
        keys = keys(file, shown);
        fonts = assets(file, shown, directory, GROUP_FONT, "a font file", "asset/font/IntelOneMono-Medium.ttf");
        icons = assets(file, shown, directory, GROUP_ICON, "an icon file", "asset/icon/chevron.png");
        dirs = assets(file, shown, directory, GROUP_DIR, "a directory", "replay");
    }

    // Each message starts with the path, as shown.
    static Settings read(Path path) {
        var shown = path.toString();
        String text;
        try {
            text = new String(Files.readAllBytes(path), StandardCharsets.UTF_8);
        } catch (NoSuchFileException exception) {
            throw new BosonogaException(shown + ": No such file or directory");
        } catch (AccessDeniedException exception) {
            throw new BosonogaException(shown + ": Permission denied");
        } catch (IOException exception) {
            throw new BosonogaException(shown + ": " + exception.getMessage());
        }
        return parse(text, shown, path.getParent() == null ? Path.of(".") : path.getParent());
    }

    static Settings parse(String text, String shown, Path directory) {
        try {
            return new Settings(KeyFile.parse(text), shown, directory);
        } catch (KeyFile.KeyFileError error) {
            throw new BosonogaException(shown + ": " + error.getMessage());
        }
    }

    // GKeyFile keeps the quotes around a value.
    private static String text(KeyFile file, String shown, String group, String key) {
        var value = file.string(group, key);
        if (value.length() >= 2 && value.startsWith("\"") && value.endsWith("\"")) {
            return value.substring(1, value.length() - 1);
        }
        return value;
    }

    private static int positive(KeyFile file, String shown, String key) {
        var value = file.integer(GROUP_GFX, key);
        if (value <= 0) {
            throw new BosonogaException("%s: %s must be positive".formatted(shown, key));
        }
        return value;
    }

    private static Map<String, Integer> colors(KeyFile file, String shown) {
        var colors = new LinkedHashMap<String, Integer>();
        if (!file.hasGroup(GROUP_COLOR)) {
            return colors;
        }
        for (var key : file.keys(GROUP_COLOR)) {
            var text = text(file, shown, GROUP_COLOR, key);
            if (text.length() != COLOR_HEX_LENGTH
                    || !text.chars().allMatch(digit -> (digit >= '0' && digit <= '9') || (digit >= 'A' && digit <= 'F'))) {
                throw new BosonogaException("%s: %s must be %d uppercase hex digits"
                        .formatted(shown, key, COLOR_HEX_LENGTH));
            }
            colors.put(key, Integer.parseInt(text, 16));
        }
        return Collections.unmodifiableMap(colors);
    }

    // A global is one number literal, as the program would write it.
    private static Map<String, Expression> globals(KeyFile file, String shown) {
        var globals = new LinkedHashMap<String, Expression>();
        if (!file.hasGroup(GROUP_GLOBAL)) {
            return globals;
        }
        for (var key : file.keys(GROUP_GLOBAL)) {
            var number = number(text(file, shown, GROUP_GLOBAL, key));
            if (number == null) {
                throw new BosonogaException("%s: %s must be one number, like 100_i32 or 100d0_f32"
                        .formatted(shown, key));
            }
            globals.put(key, number);
        }
        return Collections.unmodifiableMap(globals);
    }

    private static Expression number(String text) {
        var lexer = new BosonogaLexer(CharStreams.fromString(text));
        lexer.removeErrorListeners();
        lexer.addErrorListener(ThrowingErrorListener.INSTANCE);
        try {
            var tokens = lexer.getAllTokens();
            if (tokens.size() != 1 || tokens.getFirst().getType() != BosonogaLexer.NUMBER) {
                return null;
            }
            Token number = tokens.getFirst();
            return NumberLiteral.parse(number.getText());
        } catch (BosonogaException | NumberLiteral.Invalid exception) {
            return null;
        }
    }

    private static Map<String, String> keys(KeyFile file, String shown) {
        var keys = new LinkedHashMap<String, String>();
        if (!file.hasGroup(GROUP_KEY)) {
            return keys;
        }
        for (var key : file.keys(GROUP_KEY)) {
            var name = text(file, shown, GROUP_KEY, key);
            if (!InputKeys.NAMES.contains(name)) {
                throw new BosonogaException(("%s: %s must name a key, like SAPP_KEYCODE_W, SAPP_KEYCODE_0 or "
                        + "SAPP_KEYCODE_ESCAPE").formatted(shown, key));
            }
            keys.put(key, name);
        }
        return Collections.unmodifiableMap(keys);
    }

    // The files are not opened here, as a program that does not name one runs
    // without it. What and example say what the group names, for the error.
    private static Map<String, Asset> assets(KeyFile file, String shown, Path directory, String group,
                                             String what, String example) {
        var assets = new LinkedHashMap<String, Asset>();
        if (!file.hasGroup(group)) {
            return assets;
        }
        for (var key : file.keys(group)) {
            var text = text(file, shown, group, key);
            if (text.isEmpty()) {
                throw new BosonogaException("%s: %s must name %s, like \"%s\"".formatted(shown, key, what, example));
            }
            var path = Path.of(text).isAbsolute() ? text : directory.resolve(text).toString();
            assets.put(key, new Asset(text, path));
        }
        return Collections.unmodifiableMap(assets);
    }
}
