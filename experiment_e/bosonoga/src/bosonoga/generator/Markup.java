package bosonoga.generator;

import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;

// GLib's GMarkup parser, which the C bosonoga checks an xml_str with, so that
// a text is accepted and refused the same here, with the same messages. It is
// given the whole text at once, and its messages are without the line and
// char that GMarkup starts them with, as the C bosonoga drops those.
final class Markup {
    static final class MarkupError extends RuntimeException {
        MarkupError(String message) {
            super(message);
        }
    }

    // What the C bosonoga does with what GMarkup reads, which can refuse it.
    interface Handler {
        void start(String element, List<String> attributes);

        void end(String element);

        void passthrough(String text);
    }

    private enum State {
        START,
        AFTER_OPEN_ANGLE,
        AFTER_CLOSE_ANGLE,
        AFTER_ELISION_SLASH,
        INSIDE_OPEN_TAG_NAME,
        INSIDE_ATTRIBUTE_NAME,
        AFTER_ATTRIBUTE_NAME,
        BETWEEN_ATTRIBUTES,
        AFTER_ATTRIBUTE_EQUALS_SIGN,
        INSIDE_ATTRIBUTE_VALUE_SQ,
        INSIDE_ATTRIBUTE_VALUE_DQ,
        INSIDE_TEXT,
        AFTER_CLOSE_TAG_SLASH,
        INSIDE_CLOSE_TAG_NAME,
        AFTER_CLOSE_TAG_NAME,
        INSIDE_PASSTHROUGH,
    }

    private static final int ATTRIBUTES_MAX = 1000;
    private static final BigInteger UNSIGNED_LONG_MAX = BigInteger.ONE.shiftLeft(64).subtract(BigInteger.ONE);

    private final String text;
    private final int length;
    private final Handler handler;
    private int iter;
    private int start;
    private State state = State.START;
    private StringBuilder partial;
    private final Deque<String> tags = new ArrayDeque<>();
    private final List<String> attributes = new ArrayList<>();
    private boolean isDocumentEmpty = true;
    private int balance;

    private Markup(String text, Handler handler) {
        this.text = text;
        this.length = text.length();
        this.handler = handler;
    }

    // As g_markup_parse_context_parse and then g_markup_parse_context_end_parse.
    static void parse(String text, Handler handler) {
        var markup = new Markup(text, handler);
        markup.parseAll();
        markup.endParse();
    }

    private void parseAll() {
        while (iter != length) {
            switch (state) {
                case START -> {
                    skipSpaces();
                    if (iter != length) {
                        if (at() != '<') {
                            throw new MarkupError("Document must begin with an element (e.g. <book>)");
                        }
                        advance();
                        state = State.AFTER_OPEN_ANGLE;
                        start = iter;
                        isDocumentEmpty = false;
                    }
                }
                case AFTER_OPEN_ANGLE -> {
                    if (at() == '?' || at() == '!') {
                        addToPartial("<");
                        start = iter;
                        balance = 1;
                        state = State.INSIDE_PASSTHROUGH;
                    } else if (at() == '/') {
                        advance();
                        state = State.AFTER_CLOSE_TAG_SLASH;
                    } else if (!isCommonNameEnd(at())) {
                        state = State.INSIDE_OPEN_TAG_NAME;
                        start = iter;
                    } else {
                        throw new MarkupError(("“%s” is not a valid character following a “<” character; it may not "
                                + "begin an element name").formatted(character()));
                    }
                }
                case AFTER_CLOSE_ANGLE -> {
                    state = tags.isEmpty() ? State.START : State.INSIDE_TEXT;
                    start = iter;
                }
                case AFTER_ELISION_SLASH -> {
                    if (at() != '>') {
                        throw new MarkupError(("Odd character “%s”, expected a “>” character to end the empty-element "
                                + "tag “%s”").formatted(character(), tags.peek()));
                    }
                    advance();
                    state = State.AFTER_CLOSE_ANGLE;
                    emitEndElement();
                }
                case INSIDE_OPEN_TAG_NAME -> {
                    advanceToNameEnd();
                    addToPartial(text.substring(start, iter));
                    if (iter != length) {
                        tags.push(partial.toString());
                        partial = null;
                        state = State.BETWEEN_ATTRIBUTES;
                    }
                }
                case INSIDE_ATTRIBUTE_NAME -> {
                    advanceToNameEnd();
                    addToPartial(text.substring(start, iter));
                    if (iter != length) {
                        state = State.AFTER_ATTRIBUTE_NAME;
                    }
                }
                case AFTER_ATTRIBUTE_NAME -> {
                    skipSpaces();
                    if (iter != length) {
                        var name = partial.toString();
                        validateName(name);
                        if (attributes.size() > ATTRIBUTES_MAX) {
                            throw new MarkupError("Too many attributes in element “%s”".formatted(tags.peek()));
                        }
                        attributes.add(name);
                        partial = null;
                        if (at() != '=') {
                            throw new MarkupError(("Odd character “%s”, expected a “=” after attribute name “%s” of "
                                    + "element “%s”").formatted(character(), name, tags.peek()));
                        }
                        advance();
                        state = State.AFTER_ATTRIBUTE_EQUALS_SIGN;
                    }
                }
                case BETWEEN_ATTRIBUTES -> {
                    skipSpaces();
                    if (iter != length) {
                        if (at() == '/') {
                            advance();
                            state = State.AFTER_ELISION_SLASH;
                        } else if (at() == '>') {
                            advance();
                            state = State.AFTER_CLOSE_ANGLE;
                        } else if (!isCommonNameEnd(at())) {
                            state = State.INSIDE_ATTRIBUTE_NAME;
                            start = iter;
                        } else {
                            throw new MarkupError(("Odd character “%s”, expected a “>” or “/” character to end the "
                                    + "start tag of element “%s”, or optionally an attribute; perhaps you used an "
                                    + "invalid character in an attribute name").formatted(character(), tags.peek()));
                        }
                        if (state == State.AFTER_ELISION_SLASH || state == State.AFTER_CLOSE_ANGLE) {
                            emitStartElement();
                        }
                    }
                }
                case AFTER_ATTRIBUTE_EQUALS_SIGN -> {
                    skipSpaces();
                    if (iter != length) {
                        if (at() == '"' || at() == '\'') {
                            state = at() == '"' ? State.INSIDE_ATTRIBUTE_VALUE_DQ : State.INSIDE_ATTRIBUTE_VALUE_SQ;
                            advance();
                            start = iter;
                        } else {
                            throw new MarkupError(("Odd character “%s”, expected an open quote mark after the equals "
                                    + "sign when giving value for attribute “%s” of element “%s”")
                                    .formatted(character(), attributes.getLast(), tags.peek()));
                        }
                    }
                }
                case INSIDE_ATTRIBUTE_VALUE_SQ, INSIDE_ATTRIBUTE_VALUE_DQ -> {
                    var quote = state == State.INSIDE_ATTRIBUTE_VALUE_SQ ? '\'' : '"';
                    do {
                        if (at() == quote) {
                            break;
                        }
                    } while (advance());
                    addToPartial(text.substring(start, iter));
                    if (iter != length) {
                        unescape(partial.toString());
                        partial = null;
                        advance();
                        state = State.BETWEEN_ATTRIBUTES;
                    }
                }
                case INSIDE_TEXT -> {
                    do {
                        if (at() == '<') {
                            break;
                        }
                    } while (advance());
                    addToPartial(text.substring(start, iter));
                    if (iter != length) {
                        unescape(partial.toString());
                        advance();
                        state = State.AFTER_OPEN_ANGLE;
                        start = iter;
                        partial.setLength(0);
                    }
                }
                case AFTER_CLOSE_TAG_SLASH -> {
                    if (isCommonNameEnd(at())) {
                        throw new MarkupError(("“%s” is not a valid character following the characters “</”; “%s” "
                                + "may not begin an element name").formatted(character(), character()));
                    }
                    state = State.INSIDE_CLOSE_TAG_NAME;
                    start = iter;
                }
                case INSIDE_CLOSE_TAG_NAME -> {
                    advanceToNameEnd();
                    addToPartial(text.substring(start, iter));
                    if (iter != length) {
                        state = State.AFTER_CLOSE_TAG_NAME;
                    }
                }
                case AFTER_CLOSE_TAG_NAME -> {
                    skipSpaces();
                    if (iter != length) {
                        var name = partial.toString();
                        if (at() != '>') {
                            throw new MarkupError(("“%s” is not a valid character following the close element name "
                                    + "“%s”; the allowed character is “>”").formatted(character(), name));
                        }
                        if (tags.isEmpty()) {
                            throw new MarkupError("Element “%s” was closed, no element is currently open"
                                    .formatted(name));
                        }
                        if (!name.equals(tags.peek())) {
                            throw new MarkupError("Element “%s” was closed, but the currently open element is “%s”"
                                    .formatted(name, tags.peek()));
                        }
                        advance();
                        state = State.AFTER_CLOSE_ANGLE;
                        emitEndElement();
                        partial.setLength(0);
                    }
                }
                case INSIDE_PASSTHROUGH -> passthrough();
            }
        }
    }

    // The < and > are in the text of a passthrough, which ends at the > that
    // ends a processing instruction, a comment, a CDATA or a doctype.
    private void passthrough() {
        do {
            if (at() == '<') {
                balance++;
            }
            if (at() == '>') {
                balance--;
                addToPartial(text.substring(start, iter));
                start = iter;
                var seen = partial.toString();
                if (seen.charAt(1) == '?' && seen.endsWith("?")) {
                    break;
                }
                if (seen.startsWith("<!--") && seen.endsWith("--")) {
                    break;
                }
                if (seen.startsWith("<![CDATA[") && seen.endsWith("]]")) {
                    break;
                }
                if (seen.startsWith("<!DOCTYPE") && balance == 0) {
                    break;
                }
            }
        } while (advance());
        if (iter == length) {
            addToPartial(text.substring(start, iter));
            return;
        }
        advance();
        addToPartial(text.substring(start, iter));
        handler.passthrough(partial.toString());
        partial.setLength(0);
        state = State.AFTER_CLOSE_ANGLE;
        start = iter;
    }

    private void endParse() {
        if (isDocumentEmpty) {
            throw new MarkupError("Document was empty or contained only whitespace");
        }
        var message = switch (state) {
            case START -> null;
            case AFTER_OPEN_ANGLE -> "Document ended unexpectedly just after an open angle bracket “<”";
            case AFTER_CLOSE_ANGLE, INSIDE_TEXT -> tags.isEmpty() ? null
                    : "Document ended unexpectedly with elements still open — “%s” was the last element opened"
                            .formatted(tags.peek());
            case AFTER_ELISION_SLASH -> ("Document ended unexpectedly, expected to see a close angle bracket ending "
                    + "the tag <%s/>").formatted(tags.peek());
            case INSIDE_OPEN_TAG_NAME -> "Document ended unexpectedly inside an element name";
            case INSIDE_ATTRIBUTE_NAME, AFTER_ATTRIBUTE_NAME -> "Document ended unexpectedly inside an attribute name";
            case BETWEEN_ATTRIBUTES -> "Document ended unexpectedly inside an element-opening tag.";
            case AFTER_ATTRIBUTE_EQUALS_SIGN -> "Document ended unexpectedly after the equals sign following an "
                    + "attribute name; no attribute value";
            case INSIDE_ATTRIBUTE_VALUE_SQ, INSIDE_ATTRIBUTE_VALUE_DQ ->
                    "Document ended unexpectedly while inside an attribute value";
            case AFTER_CLOSE_TAG_SLASH, INSIDE_CLOSE_TAG_NAME, AFTER_CLOSE_TAG_NAME -> tags.isEmpty()
                    ? "Document ended unexpectedly inside the close tag for an unopened element"
                    : "Document ended unexpectedly inside the close tag for element “%s”".formatted(tags.peek());
            case INSIDE_PASSTHROUGH -> "Document ended unexpectedly inside a comment or processing instruction";
        };
        if (message != null) {
            throw new MarkupError(message);
        }
    }

    private void emitStartElement() {
        var name = tags.peek();
        validateName(name);
        handler.start(name, List.copyOf(attributes));
        attributes.clear();
    }

    private void emitEndElement() {
        handler.end(tags.peek());
        tags.pop();
    }

    // Checks the entities and the character references, as GMarkup does when
    // it unescapes a text or the value of an attribute.
    private static void unescape(String value) {
        for (int from = 0; from < value.length(); from++) {
            if (value.charAt(from) != '&') {
                continue;
            }
            from++;
            if (charAt(value, from) == '#') {
                from++;
                var base = 10;
                if (charAt(value, from) == 'x') {
                    base = 16;
                    from++;
                }
                var parsed = strtoul(value, from, base);
                if (parsed.end() == from || parsed.isOutOfRange()) {
                    throw new MarkupError(("Failed to parse “%s”, which should have been a digit inside a character "
                            + "reference (&#234; for example) — perhaps the digit is too large")
                            .formatted(value.substring(from, parsed.end())));
                }
                if (charAt(value, parsed.end()) != ';') {
                    throw new MarkupError("Character reference did not end with a semicolon; most likely you used "
                            + "an ampersand character without intending to start an entity — escape ampersand as "
                            + "&amp;");
                }
                if (!isPermitted(parsed.value())) {
                    throw new MarkupError("Character reference “%s” does not encode a permitted character"
                            .formatted(value.substring(from, parsed.end())));
                }
                from = parsed.end();
            } else if (value.startsWith("lt;", from) || value.startsWith("gt;", from)) {
                from += 2;
            } else if (value.startsWith("amp;", from)) {
                from += 3;
            } else if (value.startsWith("quot;", from) || value.startsWith("apos;", from)) {
                from += 4;
            } else if (charAt(value, from) == ';') {
                throw new MarkupError("Empty entity “&;” seen; valid entities are: &amp; &quot; &lt; &gt; &apos;");
            } else {
                var end = value.indexOf(';', from);
                if (end >= 0) {
                    throw new MarkupError("Entity name “%s” is not known".formatted(value.substring(from, end)));
                }
                throw new MarkupError("Entity did not end with a semicolon; most likely you used an ampersand "
                        + "character without intending to start an entity — escape ampersand as &amp;");
            }
        }
    }

    private static boolean isPermitted(BigInteger value) {
        var code = value.compareTo(BigInteger.valueOf(0x10FFFF)) > 0 ? -1 : value.intValue();
        return (0 < code && code <= 0xD7FF) || (0xE000 <= code && code <= 0xFFFD)
                || (0x10000 <= code && code <= 0x10FFFF);
    }

    private record Unsigned(BigInteger value, int end, boolean isOutOfRange) {
    }

    // As strtoul of C, which skips spaces and takes a sign, and in base 16 a 0x.
    private static Unsigned strtoul(String text, int from, int base) {
        var at = from;
        while (at < text.length() && " \t\n\u000b\f\r".indexOf(text.charAt(at)) >= 0) {
            at++;
        }
        var isNegative = false;
        if (at < text.length() && (text.charAt(at) == '+' || text.charAt(at) == '-')) {
            isNegative = text.charAt(at) == '-';
            at++;
        }
        if (base == 16 && charAt(text, at) == '0' && (charAt(text, at + 1) == 'x' || charAt(text, at + 1) == 'X')
                && digit(charAt(text, at + 2), 16) >= 0) {
            at += 2;
        }
        var digitsStart = at;
        var value = BigInteger.ZERO;
        while (at < text.length() && digit(text.charAt(at), base) >= 0) {
            value = value.multiply(BigInteger.valueOf(base)).add(BigInteger.valueOf(digit(text.charAt(at), base)));
            at++;
        }
        if (at == digitsStart) {
            return new Unsigned(BigInteger.ZERO, from, false);
        }
        if (value.compareTo(UNSIGNED_LONG_MAX) > 0) {
            return new Unsigned(UNSIGNED_LONG_MAX, at, true);
        }
        if (isNegative && value.signum() != 0) {
            value = UNSIGNED_LONG_MAX.add(BigInteger.ONE).subtract(value);
        }
        return new Unsigned(value, at, false);
    }

    // The first character must start a name, and those after it continue one.
    private static void validateName(String name) {
        if (name.isEmpty() || !isNameStart(name.codePointAt(0))) {
            throw new MarkupError("“%s” is not a valid name".formatted(name));
        }
        for (int i = Character.charCount(name.codePointAt(0)); i < name.length(); ) {
            var code = name.codePointAt(i);
            if (!isNameCharacter(code)) {
                // GMarkup shows the first byte of the character, which is all of an ASCII one,
                // and the C bosonoga shows another byte as \x and its hex digits.
                var first = new String(Character.toChars(code)).getBytes(StandardCharsets.UTF_8)[0] & 0xff;
                var shown = code < 0x80 ? String.valueOf((char) code) : "\\x%02x".formatted(first);
                throw new MarkupError("“%s” is not a valid name: “%s”".formatted(name, shown));
            }
            i += Character.charCount(code);
        }
    }

    private static boolean isNameStart(int code) {
        return isAsciiLetter(code) || (code != '=' && code != '/' && code != '>' && code != ' '
                && (code == '_' || code == ':' || (code >= 0x80 && Character.isLetter(code))));
    }

    private static boolean isNameCharacter(int code) {
        return isAsciiLetter(code) || (code >= '0' && code <= '9') || code == '.' || code == '-' || code == '_'
                || code == ':' || (code >= 0x80 && Character.isLetter(code));
    }

    private static boolean isAsciiLetter(int code) {
        return (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
    }

    private static boolean isCommonNameEnd(char character) {
        return character == '=' || character == '/' || character == '>' || character == ' ';
    }

    private static boolean isSpace(char character) {
        return character == ' ' || character == '\t' || character == '\n' || character == '\r';
    }

    private static int digit(char character, int base) {
        return character < 0x80 ? Character.digit(character, base) : -1;
    }

    private static char charAt(String text, int index) {
        return index < text.length() ? text.charAt(index) : '\0';
    }

    private char at() {
        return text.charAt(iter);
    }

    // The character at iter, for a message.
    private String character() {
        return new String(Character.toChars(text.codePointAt(iter)));
    }

    private boolean advance() {
        iter++;
        return iter != length;
    }

    private void skipSpaces() {
        do {
            if (!isSpace(at())) {
                return;
            }
        } while (advance());
    }

    private void advanceToNameEnd() {
        do {
            if (isCommonNameEnd(at()) || isSpace(at())) {
                return;
            }
        } while (advance());
    }

    private void addToPartial(String part) {
        if (partial == null) {
            partial = new StringBuilder();
        }
        partial.append(part);
    }
}
