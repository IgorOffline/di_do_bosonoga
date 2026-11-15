package bosonoga.generator;

import java.util.function.Supplier;

public final class GeneratorTest {
    private static int failures;

    private GeneratorTest() {
    }

    public static void main(String[] args) {
        infoPrintsI32ValuesThenNewline();
        i32BoundsAreAccepted();
        i32OverflowIsRejected();
        infoNeedsAValue();
        infoTakesOnlyI32();
        stringIsNotYetAValue();
        blockNeedsAStatement();
        shiftOrAndShiftHasBecomeBitOperations();
        shiftBitIsAtMost31();
        shiftOrTakesOnlyFlags();
        shiftHasNeedsFlags();
        nameMustBeDeclared();
        nameCannotBeDeclaredTwice();
        variableMustBeRead();
        endWithoutReturnFails();
        if (failures > 0) {
            System.err.println(failures + " test(s) failed");
            System.exit(1);
        }
    }

    private static void infoPrintsI32ValuesThenNewline() {
        var cpp = Generator.generate("// info.bosonoga\n" + program(
                "info 1_i32",
                "info -7_i32 042_i32",
                "return exit_success"), "info.bosonoga", "info");
        expectEquals("infoPrintsI32ValuesThenNewline", """
                // Generated from info.bosonoga by the bosonoga generator. Do not edit.
                #include <cstdint>
                #include <cstdlib>
                #include <iostream>

                namespace bosonoga::program {

                int info() {
                  std::cout << std::int32_t{1} << '\\n';
                  std::cout << std::int32_t{-7} << std::int32_t{42} << '\\n';
                  return EXIT_SUCCESS;
                }

                }  // namespace bosonoga::program
                """, cpp);
    }

    private static void i32BoundsAreAccepted() {
        var cpp = Generator.generate(program(
                "info 2147483647_i32 -2147483648_i32",
                "return exit_success"), "bounds.bosonoga", "bounds");
        expect("i32BoundsAreAccepted", cpp.contains(
                "std::cout << std::int32_t{2147483647} << std::int32_t{-2147483648} << '\\n';"));
    }

    private static void i32OverflowIsRejected() {
        expectError("i32OverflowIsRejected",
                "overflow.bosonoga:3:8: '2147483648_i32' does not fit in i32",
                () -> Generator.generate(program(
                        "info 2147483648_i32",
                        "return exit_success"), "overflow.bosonoga", "overflow"));
    }

    private static void infoNeedsAValue() {
        expectError("infoNeedsAValue", null, () -> Generator.generate(program(
                "info",
                "return exit_success"), "empty.bosonoga", "empty"));
    }

    private static void infoTakesOnlyI32() {
        expectError("infoTakesOnlyI32", "flags.bosonoga:4:8: info takes i32, not flags",
                () -> Generator.generate(program(
                        "var bit is shift_0",
                        "info bit",
                        "return exit_success"), "flags.bosonoga", "flags"));
    }

    private static void stringIsNotYetAValue() {
        expectError("stringIsNotYetAValue", null, () -> Generator.generate(program(
                "info \"A\"_str",
                "return exit_success"), "string.bosonoga", "string"));
    }

    private static void blockNeedsAStatement() {
        expectError("blockNeedsAStatement", null,
                () -> Generator.generate(program(), "block.bosonoga", "block"));
    }

    private static void shiftOrAndShiftHasBecomeBitOperations() {
        var cpp = Generator.generate(program(
                "var read_flag is shift_0",
                "var flags is read_flag",
                "shift_or shift_31",
                "if flags shift_has read_flag",
                "and flags shift_has shift_31",
                "di",
                "  return exit_success",
                "do",
                "return exit_failure"), "shift.bosonoga", "shift");
        expectEquals("shiftOrAndShiftHasBecomeBitOperations", """
                // Generated from shift.bosonoga by the bosonoga generator. Do not edit.
                #include <cstdint>
                #include <cstdlib>
                #include <iostream>

                namespace bosonoga::program {

                int shift() {
                  std::uint32_t v_read_flag = std::uint32_t{1U << 0};
                  std::uint32_t v_flags = v_read_flag | std::uint32_t{1U << 31};
                  if (((v_flags & v_read_flag) == v_read_flag) && ((v_flags & std::uint32_t{1U << 31}) == std::uint32_t{1U << 31})) {
                    return EXIT_SUCCESS;
                  }
                  return EXIT_FAILURE;
                }

                }  // namespace bosonoga::program
                """, cpp);
    }

    private static void shiftBitIsAtMost31() {
        expectError("shiftBitIsAtMost31",
                "bit.bosonoga:3:14: 'shift_32' is out of range, the last bit is shift_31",
                () -> Generator.generate(program(
                        "var bit is shift_32",
                        "return exit_success"), "bit.bosonoga", "bit"));
    }

    private static void shiftOrTakesOnlyFlags() {
        expectError("shiftOrTakesOnlyFlags", "or.bosonoga:3:22: shift_or takes flags, not i32",
                () -> Generator.generate(program(
                        "var count is 1_i32 shift_or shift_0",
                        "return exit_success"), "or.bosonoga", "or_"));
    }

    private static void shiftHasNeedsFlags() {
        expectError("shiftHasNeedsFlags",
                "has.bosonoga:4:12: shift_has needs flags, not i32 and i32",
                () -> Generator.generate(program(
                        "var count is 1_i32",
                        "if count shift_has count",
                        "di",
                        "  return exit_success",
                        "do",
                        "return exit_failure"), "has.bosonoga", "has"));
    }

    private static void nameMustBeDeclared() {
        expectError("nameMustBeDeclared", "name.bosonoga:3:8: 'count' is not declared",
                () -> Generator.generate(program(
                        "info count",
                        "return exit_success"), "name.bosonoga", "name"));
    }

    private static void nameCannotBeDeclaredTwice() {
        expectError("nameCannotBeDeclaredTwice", "twice.bosonoga:4:7: 'bit' is already declared",
                () -> Generator.generate(program(
                        "var bit is shift_0",
                        "var bit is shift_1",
                        "return exit_success"), "twice.bosonoga", "twice"));
    }

    private static void variableMustBeRead() {
        expectError("variableMustBeRead", "unread.bosonoga:3:7: 'bit' is never read",
                () -> Generator.generate(program(
                        "var bit is shift_0",
                        "return exit_success"), "unread.bosonoga", "unread"));
    }

    private static void endWithoutReturnFails() {
        var cpp = Generator.generate(program("info 1_i32"), "end.bosonoga", "end");
        expect("endWithoutReturnFails", cpp.contains("""
                  std::cerr << "end.bosonoga: bosonoga_main ended without a return\\n";
                  return EXIT_FAILURE;
                }
                """));
    }

    // Lines start at line 3 and column 3, after "bosonoga_main", "di" and the indent.
    private static String program(String... lines) {
        var source = new StringBuilder("bosonoga_main\ndi\n");
        for (var line : lines) {
            source.append("  ").append(line).append('\n');
        }
        return source.append("do\n").toString();
    }

    private static void expect(String name, boolean passed) {
        if (!passed) {
            failures++;
            System.err.println("FAIL " + name);
        }
    }

    private static void expectEquals(String name, String expected, String actual) {
        if (!expected.equals(actual)) {
            failures++;
            System.err.println("FAIL " + name + "\n--- expected\n" + expected + "--- actual\n" + actual);
        }
    }

    // A null message accepts any BosonogaException.
    private static void expectError(String name, String message, Supplier<String> generate) {
        try {
            generate.get();
            expect(name, false);
        } catch (BosonogaException exception) {
            if (message != null) {
                expectEquals(name, message, exception.getMessage());
            }
        }
    }
}
