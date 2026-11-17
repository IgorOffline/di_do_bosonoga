package bosonoga.generator;

import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.TreeMap;

// The registration of exactly the selected programs, so that a test that is
// compiled also runs, as in the C bosonoga's directory-based test runner.
final class TestRegistry {
    private static final String SCRATCH = "scratch";

    private TestRegistry() {
    }

    private static int rank(Path directory) {
        return switch (directory.getFileName().toString()) {
            case "silver" -> 0;
            case "gold" -> 1;
            case "platinum" -> 2;
            default -> 3;
        };
    }

    private static int compare(String left, String right) {
        return Arrays.compareUnsigned(left.getBytes(StandardCharsets.UTF_8), right.getBytes(StandardCharsets.UTF_8));
    }

    static String header(List<Path> sources) {
        var tiers = new TreeMap<Path, List<Path>>(Comparator.comparingInt(TestRegistry::rank)
                .thenComparing((left, right) -> compare(left.toString(), right.toString())));
        for (var source : sources) {
            var directory = source.getParent() == null ? Path.of(".") : source.getParent();
            tiers.computeIfAbsent(directory, ignored -> new ArrayList<>()).add(source);
        }
        var header = new StringBuilder("// Generated test registration. Do not edit.\nnamespace bosonoga::program {\n");
        var rows = new StringBuilder("#define BOSONOGA_TEST_TIERS {");
        for (var tier : tiers.entrySet()) {
            rows.append("{\"").append(CppText.escapedBytes(tier.getKey().getFileName().toString()))
                    .append("\", \"").append(CppText.escapedBytes(tier.getKey().toString())).append("\", {");
            tier.getValue().sort((left, right) -> compare(left.getFileName().toString(),
                    right.getFileName().toString()));
            for (var source : tier.getValue()) {
                var name = ProgramName.of(source.getFileName().toString());
                var symbol = ProgramName.cpp(name);
                header.append("int ").append(symbol).append("();\n");
                rows.append("{\"").append(CppText.escapedBytes(name)).append("\", bosonoga::program::")
                        .append(symbol).append("},");
            }
            rows.append("}},");
        }
        return header.append("}\n").append(rows).append("}\n").toString();
    }

    // The tier paths, regina.ini and its dirs are all relative to the directory
    // the generator runs in, so the runner runs there too, wherever it is
    // started from. It empties the dir scratch first, so that it can run again;
    // that dir must be inside the root, so that nothing else is ever removed.
    static String environment(Path root, Settings settings) {
        var scratch = "";
        var dir = settings.dirs.get(SCRATCH);
        if (dir != null) {
            var path = Path.of(dir.text()).normalize();
            if (path.isAbsolute() || path.toString().isEmpty() || path.startsWith("..")) {
                throw new BosonogaException("%s: the dir %s must be inside the directory of %s, not %s"
                        .formatted(Settings.FILE, SCRATCH, Settings.FILE, dir.text()));
            }
            scratch = path.toString();
        }
        return "#define BOSONOGA_TEST_ROOT \"%s\"\n#define BOSONOGA_TEST_SCRATCH \"%s\"\n"
                .formatted(CppText.escapedBytes(root.toString()), CppText.escapedBytes(scratch));
    }
}
