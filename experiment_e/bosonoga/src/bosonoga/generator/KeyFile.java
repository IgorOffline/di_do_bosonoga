package bosonoga.generator;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

// The part of GLib's GKeyFile that the C bosonoga reads regina.ini with, with
// its messages, so that a file means and fails the same here. A KeyFileError
// gets the name of the file from its caller.
final class KeyFile {
    static final class KeyFileError extends RuntimeException {
        private KeyFileError(String message) {
            super(message);
        }
    }

    // Each group keeps its keys in the order of the file, with the last value of
    // a key, and a group named again gets more keys.
    private final Map<String, Map<String, String>> groups = new LinkedHashMap<>();

    private KeyFile() {
    }

    static KeyFile parse(String text) {
        var file = new KeyFile();
        Map<String, String> group = null;
        var lines = text.split("\n", -1);
        for (var line : lines) {
            if (line.endsWith("\r")) {
                line = line.substring(0, line.length() - 1);
            }
            var start = 0;
            while (start < line.length() && isSpace(line.charAt(start))) {
                start++;
            }
            var content = line.substring(start);
            if (content.isEmpty() || content.charAt(0) == '#') {
                continue;
            }
            if (isGroup(content)) {
                var name = content.substring(1, content.lastIndexOf(']'));
                if (!isGroupName(name)) {
                    throw new KeyFileError("Invalid group name: " + name);
                }
                group = file.groups.computeIfAbsent(name, ignored -> new LinkedHashMap<>());
            } else if (content.indexOf('=') > 0) {
                if (group == null) {
                    throw new KeyFileError("Key file does not start with a group");
                }
                var equals = content.indexOf('=');
                var keyEnd = equals;
                while (keyEnd > 0 && isSpace(content.charAt(keyEnd - 1))) {
                    keyEnd--;
                }
                var key = content.substring(0, keyEnd);
                if (!isKeyName(key)) {
                    throw new KeyFileError("Invalid key name: " + key);
                }
                var valueStart = equals + 1;
                while (valueStart < content.length() && isSpace(content.charAt(valueStart))) {
                    valueStart++;
                }
                // A translation, as in name[de], is not for the C locale.
                if (key.indexOf('[') < 0) {
                    group.put(key, content.substring(valueStart));
                }
            } else {
                throw new KeyFileError("Key file contains line “%s” which is not a key-value pair, group, or comment"
                        .formatted(line));
            }
        }
        return file;
    }

    boolean hasGroup(String group) {
        return groups.containsKey(group);
    }

    List<String> keys(String group) {
        return List.copyOf(groups.get(group).keySet());
    }

    // Unescapes \s, \n, \t, \r and \\, as g_key_file_get_string.
    String string(String group, String key) {
        var value = value(group, key);
        var text = new StringBuilder();
        for (int i = 0; i < value.length(); i++) {
            var character = value.charAt(i);
            if (character != '\\') {
                text.append(character);
                continue;
            }
            var escaped = ++i < value.length() ? value.charAt(i) : '\0';
            switch (escaped) {
                case 's' -> text.append(' ');
                case 'n' -> text.append('\n');
                case 't' -> text.append('\t');
                case 'r' -> text.append('\r');
                case '\\' -> text.append('\\');
                default -> throw new KeyFileError(
                        "Key file contains key “%s” which has a value that cannot be interpreted.".formatted(key));
            }
        }
        return text.toString();
    }

    // As strtol in base 10, which skips leading spaces and stops at a space.
    int integer(String group, String key) {
        var value = value(group, key);
        var invalid = new KeyFileError(("Key file contains key “%s” in group “%s” which has a value that cannot "
                + "be interpreted.").formatted(key, group));
        var start = 0;
        while (start < value.length() && isSpace(value.charAt(start))) {
            start++;
        }
        var end = start;
        if (end < value.length() && (value.charAt(end) == '+' || value.charAt(end) == '-')) {
            end++;
        }
        var digitsStart = end;
        while (end < value.length() && value.charAt(end) >= '0' && value.charAt(end) <= '9') {
            end++;
        }
        if (end == digitsStart || (end < value.length() && !isSpace(value.charAt(end)))) {
            throw invalid;
        }
        try {
            return Integer.parseInt(value, start, end, 10);
        } catch (NumberFormatException exception) {
            throw invalid;
        }
    }

    private String value(String group, String key) {
        var keys = groups.get(group);
        if (keys == null) {
            throw new KeyFileError("Key file does not have group “%s”".formatted(group));
        }
        var value = keys.get(key);
        if (value == null) {
            throw new KeyFileError("Key file does not have key “%s” in group “%s”".formatted(key, group));
        }
        return value;
    }

    // Whitespace after the ] is accepted.
    private static boolean isGroup(String line) {
        if (line.charAt(0) != '[') {
            return false;
        }
        var close = line.indexOf(']');
        if (close < 0) {
            return false;
        }
        for (int i = close + 1; i < line.length(); i++) {
            if (line.charAt(i) != ' ' && line.charAt(i) != '\t') {
                return false;
            }
        }
        return true;
    }

    private static boolean isGroupName(String name) {
        return !name.isEmpty() && name.chars().noneMatch(character -> character == '[' || character == ']'
                || character < 0x20 || character == 0x7f);
    }

    // Spaces are accepted in a key but not around it, and a translation is in
    // brackets at its end.
    private static boolean isKeyName(String key) {
        var end = 0;
        while (end < key.length() && key.charAt(end) != '[' && key.charAt(end) != ']') {
            end++;
        }
        if (end == 0 || key.charAt(0) == ' ' || key.charAt(end - 1) == ' ') {
            return false;
        }
        if (end < key.length() && key.charAt(end) == '[') {
            end++;
            while (end < key.length() && (Character.isLetterOrDigit(key.charAt(end))
                    || "-_.@".indexOf(key.charAt(end)) >= 0)) {
                end++;
            }
            if (end == key.length() || key.charAt(end) != ']') {
                return false;
            }
            end++;
        }
        return end == key.length();
    }

    private static boolean isSpace(char character) {
        return character == ' ' || (character >= '\t' && character <= '\r');
    }
}
