package bosonoga.generator;

import static java.util.stream.Collectors.joining;

import java.nio.charset.StandardCharsets;
import java.util.List;

// The C++ text that the generator writes around what a program says.
final class CppText {
    static final String INDENT = "  ";

    private CppText() {
    }

    // The bytes of the text, inside a C++ string literal.
    static String escapedBytes(String text) {
        var cpp = new StringBuilder();
        for (var character : text.getBytes(StandardCharsets.UTF_8)) {
            var code = character & 0xff;
            if (code == '\\' || code == '"') {
                cpp.append('\\').append((char) code);
            } else if (code < 0x20 || code >= 0x7f) {
                cpp.append("\\%03o".formatted(code));
            } else {
                cpp.append((char) code);
            }
        }
        return cpp.toString();
    }

    // A bosonoga string has no escape sequences, so only its control
    // characters need one in C++.
    static String stdString(String content) {
        var cpp = new StringBuilder("std::string{\"");
        content.codePoints().forEach(codePoint -> {
            if (codePoint < 0x20 || codePoint == 0x7f) {
                cpp.append("\\%03o".formatted(codePoint));
            } else {
                cpp.appendCodePoint(codePoint);
            }
        });
        return cpp.append("\"}").toString();
    }

    static String rowsType(List<Kind> columns) {
        return "std::vector<std::tuple<" + types(columns) + ">>";
    }

    static String returnType(List<Kind> results) {
        return switch (results.size()) {
            case 0 -> "void";
            case 1 -> results.getFirst().cpp;
            default -> "std::tuple<" + types(results) + ">";
        };
    }

    private static String types(List<Kind> kinds) {
        return kinds.stream().map(kind -> kind.cpp).collect(joining(", "));
    }

    // The program's bosonoga_main is private to its file, and runs as
    // bosonoga::program::<exported>, which returns its exit status.
    static String program(String sourceName, String sources, CharSequence definitions, CharSequence body,
                          String exported) {
        return """
                // Generated from %s by the bosonoga generator. Do not edit.
                #include "bosonoga_runtime.hh"

                namespace {

                namespace runtime = bosonoga::runtime;

                """.formatted(escapedBytes(sourceName)) + sources + definitions + "int bosonoga_main() {\n" + body + """
                }

                }  // namespace

                namespace bosonoga::program {

                int %s() {
                  try {
                    return ::bosonoga_main();
                  } catch (const ::bosonoga::runtime::Error &error) {
                    std::cerr << error.what() << '\\n';
                    return EXIT_FAILURE;
                  }
                }

                }  // namespace bosonoga::program
                """.formatted(exported);
    }
}
