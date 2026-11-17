package bosonoga.generator;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;

public final class Main {
    private Main() {
    }

    // Each NAME.bosonoga becomes NAME.cc. ProgramName makes its C++ export safe.
    public static void main(String[] args) {
        if (args.length < 2) {
            System.err.println("usage: bosonoga <output directory> <input.bosonoga>...\n"
                    + "   or: bosonoga <output directory> --sources-from <NUL-separated file>");
            System.exit(2);
        }
        var outputDirectory = Path.of(args[0]);
        var names = new HashSet<String>();
        try {
            // As the C bosonoga, from the directory that it runs in.
            var settings = Settings.read(Path.of(Settings.FILE));
            Files.createDirectories(outputDirectory);
            var selected = inputs(args);
            for (var input : selected) {
                var fileName = input.getFileName().toString();
                if (!fileName.endsWith(ProgramName.SUFFIX)) {
                    fail("%s does not end with %s".formatted(input, ProgramName.SUFFIX));
                }
                var name = ProgramName.of(fileName);
                if (!names.add(name)) {
                    fail("more than one program is named %s".formatted(name));
                }
                var cpp = Generator.generate(Files.readString(input), input.toString(), name, settings);
                Files.writeString(outputDirectory.resolve(name + ".cc"), cpp);
            }
            if (args[1].equals("--sources-from")) {
                Files.writeString(outputDirectory.resolve("programs.hh"), TestRegistry.header(selected)
                        + TestRegistry.environment(Path.of("").toAbsolutePath(), settings));
            }
        } catch (BosonogaException exception) {
            System.err.println(exception.getMessage());
            System.exit(1);
        } catch (IOException exception) {
            fail(exception.getMessage());
        }
    }

    // A source list is NUL-separated, so that paths keep their whitespace, and
    // its programs are the files that no file of the list imports.
    static List<Path> inputs(String[] args) throws IOException {
        if (args[1].equals("--sources-from")) {
            if (args.length != 3) {
                throw new BosonogaException("--sources-from needs one NUL-separated file");
            }
            var text = Files.readString(Path.of(args[2]));
            if (!text.isEmpty() && !text.endsWith("\0")) {
                throw new BosonogaException("source list must end with NUL");
            }
            var paths = new ArrayList<Path>();
            if (!text.isEmpty()) {
                var names = text.substring(0, text.length() - 1).split("\0", -1);
                for (var name : names) {
                    if (name.isEmpty()) {
                        throw new BosonogaException("source list contains an empty path");
                    }
                    paths.add(Path.of(name));
                }
            }
            return TestSources.programs(paths);
        }
        var paths = new ArrayList<Path>();
        for (int i = 1; i < args.length; i++) {
            paths.add(Path.of(args[i]));
        }
        return List.copyOf(paths);
    }

    private static void fail(String message) {
        System.err.println("bosonoga: " + message);
        System.exit(1);
    }
}
