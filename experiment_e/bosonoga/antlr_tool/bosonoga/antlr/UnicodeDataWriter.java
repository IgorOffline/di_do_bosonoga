package bosonoga.antlr;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import org.antlr.v4.unicode.UnicodeDataTemplateController;
import org.stringtemplate.v4.STGroupDir;

// Does what string-template-maven-plugin does in the ANTLR tool's own build:
// renders unicodedata.st with the Unicode properties that icu4j knows.
public final class UnicodeDataWriter {
    private UnicodeDataWriter() {
    }

    public static void main(String[] args) throws IOException {
        if (args.length != 2) {
            System.err.println("usage: UnicodeDataWriter <template directory> <UnicodeData.java>");
            System.exit(2);
        }
        var template = new STGroupDir(args[0]).getInstanceOf("unicodedata");
        if (template == null) {
            System.err.println("UnicodeDataWriter: no unicodedata.st in " + args[0]);
            System.exit(1);
        }
        UnicodeDataTemplateController.getProperties().forEach(template::add);
        var output = Path.of(args[1]);
        Files.createDirectories(output.toAbsolutePath().getParent());
        // The template escapes > as \>, which ST4 4.3.4 unescapes but the 4.3.1
        // inside the ANTLR 3 complete jar leaves as is.
        Files.writeString(output, template.render().replace("\\>", ">"));
    }
}
