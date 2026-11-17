package bosonoga.generator;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

final class GfxGeneratorTest {
    private static final String MAIN = "gfx_main di return exit_success do";
    private static final Settings SETTINGS = Settings.parse("""
            [meta]
            name = "graphics checks"
            version = "0.2.0"
            [color]
            night = "0A0A23"
            [font]
            mono = "font.ttf"
            [icon]
            dot = "dot.png"
            [key]
            jump = "SAPP_KEYCODE_SPACE"
            """, "regina.ini", Path.of("."));
    private static int failures;

    private GfxGeneratorTest() {
    }

    static int run() {
        reservedWords();
        sections();
        drawing();
        specialValues();
        return failures;
    }

    private static String generate(String source) {
        return Generator.generate(source, "gfx.bosonoga", "graphics_probe", SETTINGS);
    }

    private static void error(String source, int offset, String message) {
        var before = source.substring(0, offset);
        var line = before.chars().filter(c -> c == '\n').count() + 1;
        var column = before.substring(before.lastIndexOf('\n') + 1).getBytes(StandardCharsets.UTF_8).length + 1;
        var expected = "gfx.bosonoga:" + line + ":" + column + ": " + message;
        try {
            generate(source);
            fail("accepted: " + source);
        } catch (BosonogaException exception) {
            if (!expected.equals(exception.getMessage())) {
                fail("expected: " + expected + "\nactual: " + exception.getMessage());
            }
        }
    }

    private static void fail(String message) {
        failures++;
        System.err.println("FAIL gfx: " + message);
    }

    private static void valid(String name, String source, String expectedCpp) {
        try {
            var cpp = generate(source);
            if (!cpp.contains(expectedCpp)) fail(name + " did not emit " + expectedCpp);
            var directory = Path.of("build/bosonoga/gfx_checks");
            Files.createDirectories(directory);
            Files.writeString(directory.resolve(name + ".cc"), cpp);
        } catch (BosonogaException | IOException exception) {
            fail(name + ": " + exception.getMessage());
        }
    }

    private static void reservedWords() {
        for (var word : new String[]{"gfx_init", "gfx_frame", "gfx_cleanup", "gfx_key_down", "gfx_main",
                "gfx_clear", "gfx_draw_rectangle", "gfx_draw_hex", "gfx_draw_circle", "gfx_draw_text",
                "gfx_draw_icon", "gfx_await_next_frame", "gfx_delta_time", "gfx_key"}) {
            var source = "bosonoga_main di var " + word + " is 1_i32 info " + word + " return exit_success do";
            error(source, source.indexOf(word), "expected a variable name, got '" + word + "'");
        }
        generate("bosonoga_main di var left is 1_i32 info left return exit_success do");
        generate("bosonoga_main di var gfx_custom is 1_i32 info gfx_custom return exit_success do");
        var multiple = "bosonoga_main di var x gfx_custom is 1_i32 return exit_success do";
        error(multiple, multiple.indexOf("gfx_custom"), "a var declares one variable, and the values of a call "
                + "are taken with call ... with ret, one per line");
    }

    private static void sections() {
        valid("main", MAIN, "the console runner requires bosonoga_main");
        var missing = "gfx_init di nihil do";
        error(missing, missing.length(), "a gfx program needs a gfx_main block");
        for (var section : new String[]{"gfx_init", "gfx_frame", "gfx_cleanup", "gfx_key_down", "gfx_main"}) {
            var block = section + " di " + (section.equals("gfx_main") ? "return exit_success" : "nihil") + " do";
            var source = block + "\n" + block;
            error(source, source.lastIndexOf(section), "there is already a " + section + " block");
        }
        var mixed = "bosonoga_main di return exit_success do\ngfx_frame di nihil do";
        error(mixed, mixed.indexOf("gfx_frame"),
                "expected const, def, val or end of file, got 'gfx_frame'");
        mixed = "gfx_frame di nihil do\nbosonoga_main di return exit_success do";
        error(mixed, mixed.indexOf("bosonoga_main"),
                "expected const, def, val, gfx_init, gfx_frame, gfx_cleanup, "
                        + "gfx_key_down or gfx_main, got 'bosonoga_main'");
        var global = "gfx_init di nihil do\nbosonoga_global di nihil do\n" + MAIN;
        error(global, global.indexOf("bosonoga_global"), "bosonoga_global must be the first section");
        var returns = "gfx_init di return exit_success do\n" + MAIN;
        error(returns, returns.indexOf("return"), "only bosonoga_main and gfx_main can return");
        valid("globals", "bosonoga_global di var count is 1_i32 do\ngfx_frame di info count do\n" + MAIN,
                "runtime::info(std::cout, v_count)");
    }

    private static void drawing() {
        for (var word : new String[]{"gfx_clear", "gfx_draw_rectangle", "gfx_draw_hex", "gfx_draw_circle",
                "gfx_draw_text", "gfx_draw_icon"}) {
            var source = "bosonoga_main di " + word + " do";
            error(source, source.indexOf(word), word + " can only be in gfx_frame");
        }
        var source = "gfx_frame di gfx_draw_rectangle 0_i32 do\n" + MAIN;
        error(source, source.indexOf("0_i32"), "gfx_draw_rectangle needs f32, but '0_i32' is i32");
        source = "gfx_frame di gfx_clear do\n" + MAIN;
        error(source, source.indexOf(" do") + 1, "expected a value, got 'do'");
        source = "gfx_frame di gfx_clear color_night 1_i32 do\n" + MAIN;
        error(source, source.indexOf("1_i32"), "expected a statement, got '1_i32'");
        source = "gfx_frame di gfx_draw_text blue top do\n" + MAIN;
        error(source, source.indexOf("blue"), "expected left, center or right, got 'blue'");
        source = "gfx_frame di gfx_draw_text left blue do\n" + MAIN;
        error(source, source.indexOf("blue"), "expected top, middle, baseline or bottom, got 'blue'");
        source = "gfx_frame di gfx_draw_text left top 0d0_f32 0d0_f32 font_mono 12d0_f32 color_night do\n" + MAIN;
        error(source, source.indexOf(" do") + 1, "expected a value to draw, got 'do'");
        var draw = """
                gfx_init di nihil do
                gfx_frame di
                  gfx_clear color_night
                  gfx_draw_rectangle 0d0_f32 0d0_f32 2d0_f32 3d0_f32 color_night
                  gfx_draw_hex 0d0_f32 0d0_f32 1d0_f32 color_night
                  gfx_draw_circle 0d0_f32 0d0_f32 1d0_f32 color_night
                  gfx_draw_icon 0d0_f32 0d0_f32 icon_dot 0d0_f32 color_night
                  var text is new str of "text"_str
                  gfx_draw_text center baseline 0d0_f32 0d0_f32 font_mono 12d0_f32 color_night text 1_i32
                  unset text
                do
                gfx_cleanup di nihil do
                gfx_main di gfx_await_next_frame return exit_success do
                """;
        valid("draw", draw, "gfx_window.scene.draw_text");
        valid("setup", draw, "runtime::gfx::Window gfx_window({\"gfx.bosonoga\", 0, 0, runtime::Color{0x000000U}, "
                + "{{\"mono\", \"./font.ttf\"}}, {{\"dot\", \"./dot.png\"}}});");
        valid("sections", draw, "  gfx_window.cleanup = [&]() {\n  // nihil\n  };\n  gfx_window.main = [&]() -> int {\n"
                + "  runtime::gfx::next_frame();\n  return EXIT_SUCCESS;\n  };\n  return gfx_window.run();");
        valid("entry", MAIN, "int gfx_program() { return ::bosonoga_gfx(); }");
    }

    private static void specialValues() {
        for (var word : new String[]{"gfx_delta_time", "gfx_key"}) {
            var required = word.equals("gfx_key") ? "gfx_key_down" : "gfx_frame";
            var source = "bosonoga_main di info " + word + " return exit_success do";
            error(source, source.indexOf(word), word + " can only be in " + required);
        }
        var source = "def test_fn ret_void di gfx_await_next_frame do\n" + MAIN;
        error(source, source.indexOf("gfx_await_next_frame"), "gfx_await_next_frame can only be in gfx_main");
        source = "gfx_frame di nihil do\ndef test_fn ret_void di info gfx_delta_time do\n" + MAIN;
        error(source, source.indexOf("gfx_delta_time"), "gfx_delta_time can only be in gfx_frame");
        source = "gfx_frame di nihil do\nval rows is array 0_f32 di row gfx_delta_time do\n" + MAIN;
        error(source, source.indexOf("gfx_delta_time"), "gfx_delta_time can only be in gfx_frame");
        valid("values", "gfx_frame di info gfx_delta_time do\n"
                + "gfx_key_down di if gfx_key eq key_jump di info gfx_key do do\n" + MAIN,
                "gfx_window.scene.key");
    }
}
