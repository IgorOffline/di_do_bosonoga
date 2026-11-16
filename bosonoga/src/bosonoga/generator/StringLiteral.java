package bosonoga.generator;

import org.antlr.v4.runtime.Token;

// What the C bosonoga checks of a string literal as it reads it, before the
// tokens after it, so that its errors come first.
final class StringLiteral {
    static final String STRING_SUFFIX = "_str";
    static final String XML_STRING_SUFFIX = "_xml_str";

    private StringLiteral() {
    }

    // The kind of a STRING token, which has its text between its quotes.
    static Kind check(Token literal) {
        var text = literal.getText();
        var source = literal.getInputStream().getSourceName();
        for (int i = 1; i < text.length() && text.charAt(i) != '"'; i++) {
            var character = text.charAt(i);
            if (character == '\\' || character == '\0') {
                throw new BosonogaException(source, literal.getLine(),
                        Columns.at(literal.getInputStream(), literal.getStartIndex() + i),
                        character == '\\' ? "escape sequences are not supported" : "NUL byte in string");
            }
        }
        if (literal.getType() == BosonogaLexer.UNTERMINATED_STRING) {
            throw BosonogaException.at(literal, "unterminated string");
        }
        var end = text.lastIndexOf('"');
        var suffix = text.substring(end + 1);
        if (suffix.equals(XML_STRING_SUFFIX)) {
            var problem = Xml.fragmentProblem(text.substring(1, end));
            if (problem.isPresent()) {
                throw BosonogaException.at(literal, "an xml_str is well-formed XML, but " + problem.get());
            }
            return Kind.XML_STR;
        }
        if (!suffix.equals(STRING_SUFFIX)) {
            throw BosonogaException.at(literal, "a string must have the suffix _str or _xml_str, as in \"text\"_str");
        }
        return Kind.STR;
    }
}
