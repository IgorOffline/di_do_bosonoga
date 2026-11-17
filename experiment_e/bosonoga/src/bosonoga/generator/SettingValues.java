package bosonoga.generator;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

// A name of regina.ini, as in color_night, is a literal, and not a name of the
// program. As in the C bosonoga, a name that regina.ini does not have is a name
// of the program.
final class SettingValues {
    private static final String COLOR_PREFIX = "color_";
    private static final String GLOBAL_PREFIX = "global_";
    private static final String KEY_PREFIX = "key_";
    private static final String FONT_PREFIX = "font_";
    private static final String ICON_PREFIX = "icon_";
    private static final String DIR_PREFIX = "dir_";

    private final Settings settings;
    // As in the C bosonoga, the program numbers the fonts, icons and
    // directories that it names, in the order it names them.
    private final List<String> fonts = new ArrayList<>();
    private final List<String> icons = new ArrayList<>();
    private final List<String> dirs = new ArrayList<>();

    SettingValues(Settings settings) {
        this.settings = settings;
    }

    boolean has(String text) {
        return has(settings.colors, COLOR_PREFIX, text) || has(settings.globals, GLOBAL_PREFIX, text)
                || has(settings.keys, KEY_PREFIX, text) || has(settings.fonts, FONT_PREFIX, text)
                || has(settings.icons, ICON_PREFIX, text) || has(settings.dirs, DIR_PREFIX, text);
    }

    // Of a name that has is true of.
    Expression value(String text) {
        if (has(settings.colors, COLOR_PREFIX, text)) {
            var color = settings.colors.get(text.substring(COLOR_PREFIX.length()));
            return new Expression("runtime::Color{0x%06XU}".formatted(color), Kind.COLOR);
        }
        if (has(settings.globals, GLOBAL_PREFIX, text)) {
            return settings.globals.get(text.substring(GLOBAL_PREFIX.length()));
        }
        if (has(settings.keys, KEY_PREFIX, text)) {
            return new Expression("runtime::InputKey{\"%s\"}".formatted(settings.keys.get(
                    text.substring(KEY_PREFIX.length()))), Kind.INPUT_KEY);
        }
        if (has(settings.fonts, FONT_PREFIX, text)) {
            return asset(text.substring(FONT_PREFIX.length()), settings.fonts, fonts, Kind.FONT);
        }
        if (has(settings.icons, ICON_PREFIX, text)) {
            return asset(text.substring(ICON_PREFIX.length()), settings.icons, icons, Kind.ICON);
        }
        return asset(text.substring(DIR_PREFIX.length()), settings.dirs, dirs, Kind.DIR);
    }

    Settings settings() {
        return settings;
    }

    // The fonts or icons that the program names, by key, in the order of their index.
    Map<String, Settings.Asset> named(Kind kind) {
        var keys = kind == Kind.FONT ? fonts : icons;
        var group = kind == Kind.FONT ? settings.fonts : settings.icons;
        var named = new LinkedHashMap<String, Settings.Asset>();
        for (var key : keys) {
            named.put(key, group.get(key));
        }
        return named;
    }

    private static boolean has(Map<String, ?> group, String prefix, String text) {
        return text.startsWith(prefix) && group.containsKey(text.substring(prefix.length()));
    }

    private static Expression asset(String key, Map<String, Settings.Asset> assets, List<String> named, Kind kind) {
        if (!named.contains(key)) {
            named.add(key);
        }
        var asset = assets.get(key);
        return new Expression("%s{%dU, \"%s\", \"%s\"}".formatted(kind.cpp, named.indexOf(key),
                CppText.escapedBytes(asset.text()), CppText.escapedBytes(asset.path())), kind);
    }
}
