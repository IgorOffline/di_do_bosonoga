package bosonoga.generator;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.regex.Pattern;

public final class Main {
    private static final String EXTENSION = ".bosonoga";
    private static final Pattern CPP_NAME = Pattern.compile("[A-Za-z_][A-Za-z0-9_]*");

    private Main() {
    }

    // Each NAME.bosonoga becomes NAME.cc, with its bosonoga_main as the function NAME.
    public static void main(String[] args) {
        if (args.length < 2) {
            System.err.println("usage: bosonoga <output directory> <input.bosonoga>...");
            System.exit(2);
        }
        var outputDirectory = Path.of(args[0]);
        var names = new HashSet<String>();
        try {
            // As the C bosonoga, from the directory that it runs in.
            var settings = Settings.read(Path.of(Settings.FILE));
            Files.createDirectories(outputDirectory);
            for (int i = 1; i < args.length; i++) {
                var input = Path.of(args[i]);
                var fileName = input.getFileName().toString();
                if (!fileName.endsWith(EXTENSION)) {
                    fail("%s does not end with %s".formatted(input, EXTENSION));
                }
                var name = fileName.substring(0, fileName.length() - EXTENSION.length());
                if (!CPP_NAME.matcher(name).matches()) {
                    fail("%s needs a name of letters, digits and _".formatted(input));
                }
                if (!names.add(name)) {
                    fail("more than one program is named %s".formatted(name));
                }
                var cpp = Generator.generate(Files.readString(input), input.toString(), name, settings);
                Files.writeString(outputDirectory.resolve(name + ".cc"), cpp);
            }
        } catch (BosonogaException exception) {
            System.err.println(exception.getMessage());
            System.exit(1);
        } catch (IOException exception) {
            fail(exception.getMessage());
        }
    }

    private static void fail(String message) {
        System.err.println("bosonoga: " + message);
        System.exit(1);
    }
}
