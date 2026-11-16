package bosonoga.generator;

import java.nio.charset.StandardCharsets;
import java.util.HexFormat;
import java.util.Set;
import java.util.regex.Pattern;

// A program is named by its file, NAME.bosonoga. As file names are not always
// C++ identifiers, the C++ export keeps an ordinary name and encodes any other
// with a prefix that ordinary exports cannot occupy.
final class ProgramName {
    static final String SUFFIX = ".bosonoga";
    static final String EXPECTED_SUFFIX = ".xp.bosonoga";
    static final String MODULE_SUFFIX = ".mod.bosonoga";

    private static final String PREFIX = "bosonoga_program_";
    private static final Pattern IDENTIFIER = Pattern.compile("[A-Za-z_][A-Za-z0-9_]*");
    private static final Set<String> KEYWORDS = Set.of(("alignas alignof and and_eq asm auto bitand bitor bool "
            + "break case catch char char16_t char32_t class compl const constexpr const_cast continue decltype "
            + "default delete do double dynamic_cast else enum explicit export extern false float for friend goto "
            + "if inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private "
            + "protected public register reinterpret_cast return short signed sizeof static static_assert "
            + "static_cast struct switch template this thread_local throw true try typedef typeid typename union "
            + "unsigned using virtual void volatile wchar_t while xor xor_eq").split(" "));

    private ProgramName() {
    }

    // Of a file name that ends with SUFFIX.
    static String of(String fileName) {
        return fileName.substring(0, fileName.length() - SUFFIX.length());
    }

    static String cpp(String name) {
        if (IDENTIFIER.matcher(name).matches() && !KEYWORDS.contains(name) && !name.startsWith(PREFIX)
                && !name.contains("__") && !name.matches("_[A-Z].*")) {
            return name;
        }
        return PREFIX + HexFormat.of().formatHex(name.getBytes(StandardCharsets.UTF_8));
    }
}
