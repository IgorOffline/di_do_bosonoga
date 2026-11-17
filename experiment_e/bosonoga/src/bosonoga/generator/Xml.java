package bosonoga.generator;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;

// What the C bosonoga checks of an xml_str, a fragment that GMarkup reads
// without comments, CDATA, processing instructions, a doctype or a < in an
// attribute.
final class Xml {
    // A fragment is read inside an element of this name, so that it may have
    // text and many elements at its top.
    private static final String WRAPPER = "bosonoga_fragment";
    private static final int PASSTHROUGH_SHOWN_MAX = 32;

    private Xml() {
    }

    // Of an element or an attribute that a program names.
    static boolean isName(String text) {
        if (text.isEmpty() || !(isAsciiLetter(text.charAt(0)) || text.charAt(0) == '_')) {
            return false;
        }
        return text.chars().skip(1).allMatch(character -> isAsciiLetter(character)
                || (character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.');
    }

    // Why the text is not an xml_str, in the words of the C bosonoga.
    static Optional<String> fragmentProblem(String text) {
        if (text.indexOf('\0') >= 0) {
            return Optional.of("an xml_str has no NUL byte");
        }
        var open = new ArrayList<String>();
        var wrapped = "<" + WRAPPER + ">" + text + "</" + WRAPPER + ">";
        String message;
        try {
            Markup.parse(wrapped, new FragmentCheck(open));
            message = quoteProblem(wrapped);
        } catch (Markup.MarkupError error) {
            message = error.getMessage();
        }
        if (message == null) {
            return Optional.empty();
        }
        // GMarkup may name the element around the text.
        if (message.contains(WRAPPER)) {
            return Optional.of(open.size() > 1
                    ? "'%s' is not closed".formatted(open.getLast())
                    : "an element is closed that was not opened");
        }
        return Optional.of(message);
    }

    private static final class FragmentCheck implements Markup.Handler {
        private final List<String> open;
        private boolean isClosed;

        private FragmentCheck(List<String> open) {
            this.open = open;
        }

        @Override
        public void start(String element, List<String> attributes) {
            if (open.isEmpty() && isClosed) {
                throw new Markup.MarkupError("an element is closed that was not opened before '%s'".formatted(element));
            }
            for (int i = 0; i < attributes.size(); i++) {
                if (attributes.subList(0, i).contains(attributes.get(i))) {
                    throw new Markup.MarkupError("'%s' has the attribute '%s' twice"
                            .formatted(element, attributes.get(i)));
                }
            }
            open.add(element);
        }

        @Override
        public void end(String element) {
            open.removeLast();
            if (open.isEmpty()) {
                isClosed = true;
            }
        }

        @Override
        public void passthrough(String text) {
            var bytes = text.getBytes(StandardCharsets.UTF_8);
            var shown = new String(Arrays.copyOf(bytes, Math.min(bytes.length, PASSTHROUGH_SHOWN_MAX)),
                    StandardCharsets.UTF_8);
            throw new Markup.MarkupError(("an xml_str has no comment, CDATA, processing instruction or doctype, "
                    + "but '%s'").formatted(shown));
        }
    }

    // GMarkup takes a < in an attribute, which XML does not.
    private static String quoteProblem(String text) {
        var i = 0;
        while (i < text.length()) {
            if (text.charAt(i) != '<') {
                i++;
                continue;
            }
            for (i++; i < text.length() && text.charAt(i) != '>'; i++) {
                var quote = text.charAt(i);
                if (quote != '"' && quote != '\'') {
                    continue;
                }
                for (i++; i < text.length() && text.charAt(i) != quote; i++) {
                    if (text.charAt(i) == '<') {
                        return "an attribute has a <, which it writes as &lt;";
                    }
                }
            }
        }
        return null;
    }

    private static boolean isAsciiLetter(int character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
    }
}
