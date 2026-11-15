package bosonoga.generator;

import java.util.function.Supplier;

public final class GeneratorTest {
    private static final String BODY_START = "int bosonoga_main() {\n";
    private static final String BODY_END = "}\n\n}  // namespace\n";

    private static int failures;

    private GeneratorTest() {
    }

    public static void main(String[] args) {
        infoIsAFunctionOfTheProgram();
        numbersOfEveryKind();
        numberMustFitItsKind();
        unsignedIsNotNegative();
        numberNeedsASuffix();
        integerHasNoFraction();
        f32MustFit();
        infoNeedsAValue();
        xmlStrIsNotYetAValue();
        blockNeedsAStatement();
        shiftOrAndShiftHasBecomeBitOperations();
        shiftBitIsAtMost31();
        shiftOrNeedsFlags();
        shiftHasNeedsFlags();
        nameMustBeDeclared();
        nameCannotBeDeclaredTwice();
        variableMustBeRead();
        valCannotBeSet();
        setToTakesOneValue();
        setToNeedsTheSameKind();
        accumulationIsChecked();
        flagsHaveNoArithmetic();
        accumulationNeedsTheSameKind();
        toModNeedsAValueToDivideBy();
        mathNeedsAReal();
        setMustGetItsKind();
        operationsAreChecked();
        comparisonNeedsTheSameKind();
        realHasNoExactEq();
        integerHasNoTolerance();
        andBindsTighterThanOr();
        realIsCloseWithinATolerance();
        elseIfAndElse();
        endWithoutReturnFails();
        countedLoopCopiesItsCounter();
        whileLoopsWithBreakAndContinue();
        breakNeedsALoop();
        loopCountFitsIn32Bits();
        loopVariableMustBeRead();
        boolIsACondition();
        onlyABoolIsACondition();
        boolComparesOnlyWithEq();
        stringIsItsText();
        stringHasNoEscapeSequences();
        stringNeedsItsSuffix();
        constantIsWrittenWhereItIsRead();
        constantMustBeUsed();
        constantCannotBeSet();
        constantNameIsTaken();
        programNeedsMain();
        programHasOneMain();
        sleepTakesU32();
        if (failures > 0) {
            System.err.println(failures + " test(s) failed");
            System.exit(1);
        }
    }

    private static void infoIsAFunctionOfTheProgram() {
        var cpp = Generator.generate("// info.bosonoga\n" + program(
                "info 1_i32",
                "info -7_i32 042_i32",
                "return exit_success"), "info.bosonoga", "info");
        expectEquals("infoIsAFunctionOfTheProgram", """
                // Generated from info.bosonoga by the bosonoga generator. Do not edit.
                #include "bosonoga_runtime.hh"

                namespace {

                namespace runtime = bosonoga::runtime;

                int bosonoga_main() {
                  runtime::info(std::cout, std::int32_t{1});
                  runtime::info(std::cout, std::int32_t{-7}, std::int32_t{42});
                  return EXIT_SUCCESS;
                }

                }  // namespace

                namespace bosonoga::program {

                int info() {
                  try {
                    return ::bosonoga_main();
                  } catch (const ::bosonoga::runtime::Error &error) {
                    std::cerr << error.what() << '\\n';
                    return EXIT_FAILURE;
                  }
                }

                }  // namespace bosonoga::program
                """, cpp);
    }

    private static void numbersOfEveryKind() {
        expectBody("numbersOfEveryKind", """
                  runtime::info(std::cout, std::int32_t{1}, std::int64_t{-9223372036854775807LL - 1}, std::uint32_t{4000000000U}, std::uint64_t{18446744073709551615ULL}, 0x1.99999ap-4F, 0x1.8p0);
                  return EXIT_SUCCESS;
                """, program(
                "info 1_i32 -9223372036854775808_i64 4000000000_u32 18446744073709551615_u64 0d1_f32 1d5_f64",
                "return exit_success"));
    }

    private static void numberMustFitItsKind() {
        expectError("numberMustFitItsKind", "t.bosonoga:3:8: the number is outside the i32 range",
                program("info 2147483648_i32", "return exit_success"));
    }

    private static void unsignedIsNotNegative() {
        expectError("unsignedIsNotNegative", "t.bosonoga:3:8: the number is outside the u32 range",
                program("info -1_u32", "return exit_success"));
    }

    private static void numberNeedsASuffix() {
        expectError("numberNeedsASuffix",
                "t.bosonoga:3:8: number must have the suffix _i32, _i64, _u32, _u64, _f32 or _f64",
                program("info 5", "return exit_success"));
    }

    private static void integerHasNoFraction() {
        expectError("integerHasNoFraction", "t.bosonoga:3:8: an integer cannot have a fraction",
                program("info 1d5_i32", "return exit_success"));
    }

    private static void f32MustFit() {
        expectError("f32MustFit", "t.bosonoga:3:8: the number is outside the f32 range",
                program("info 350000000000000000000000000000000000000d0_f32", "return exit_success"));
    }

    private static void infoNeedsAValue() {
        expectError("infoNeedsAValue", null, program("info", "return exit_success"));
    }

    private static void xmlStrIsNotYetAValue() {
        expectError("xmlStrIsNotYetAValue",
                "t.bosonoga:3:8: an xml_str is not supported yet, but a str is, as in \"text\"_str",
                program("info \"<a/>\"_xml_str", "return exit_success"));
    }

    private static void blockNeedsAStatement() {
        expectError("blockNeedsAStatement", null, program());
    }

    private static void shiftOrAndShiftHasBecomeBitOperations() {
        expectBody("shiftOrAndShiftHasBecomeBitOperations", """
                  std::uint32_t v_read_flag = std::uint32_t{1U << 0};
                  std::uint32_t v_flags = v_read_flag | std::uint32_t{1U << 31};
                  if (((v_flags & v_read_flag) == v_read_flag) && ((v_flags & std::uint32_t{1U << 31}) == std::uint32_t{1U << 31})) {
                    return EXIT_SUCCESS;
                  }
                  return EXIT_FAILURE;
                """, program(
                "var read_flag is shift_0",
                "var flags is read_flag",
                "shift_or shift_31",
                "if flags shift_has read_flag",
                "and flags shift_has shift_31",
                "di",
                "  return exit_success",
                "do",
                "return exit_failure"));
    }

    private static void shiftBitIsAtMost31() {
        expectError("shiftBitIsAtMost31",
                "t.bosonoga:3:14: 'shift_32' is out of range, the last bit is shift_31",
                program("var bit is shift_32", "return exit_success"));
    }

    private static void shiftOrNeedsFlags() {
        expectError("shiftOrNeedsFlags", "t.bosonoga:3:16: shift_or needs flags, but '1_i32' is i32",
                program("var count is 1_i32 shift_or shift_0", "return exit_success"));
    }

    private static void shiftHasNeedsFlags() {
        expectError("shiftHasNeedsFlags",
                "t.bosonoga:4:12: shift_has needs flags, not i32 and i32",
                program(
                        "var count is 1_i32",
                        "if count shift_has count",
                        "di",
                        "  return exit_success",
                        "do",
                        "return exit_failure"));
    }

    private static void nameMustBeDeclared() {
        expectError("nameMustBeDeclared", "t.bosonoga:3:8: unknown variable 'count'",
                program("info count", "return exit_success"));
    }

    private static void nameCannotBeDeclaredTwice() {
        expectError("nameCannotBeDeclaredTwice", "t.bosonoga:4:7: variable 'bit' already exists",
                program("var bit is shift_0", "var bit is shift_1", "return exit_success"));
    }

    private static void variableMustBeRead() {
        expectError("variableMustBeRead", "t.bosonoga:3:7: 'bit' is never read",
                program("var bit is shift_0", "return exit_success"));
    }

    private static void valCannotBeSet() {
        expectError("valCannotBeSet", "t.bosonoga:4:7: 'count' is a val and cannot be set",
                program("val count is 1_i32", "set count to 2_i32", "info count", "return exit_success"));
    }

    private static void setToTakesOneValue() {
        expectError("setToTakesOneValue",
                "t.bosonoga:4:22: set ... to sets 'count' to a single value",
                program("var count is 1_i32", "set count to 2_i32 3_i32", "info count",
                        "return exit_success"));
    }

    private static void setToNeedsTheSameKind() {
        expectError("setToNeedsTheSameKind",
                "t.bosonoga:4:16: set ... to needs i32, but '2_i64' is i64",
                program("var count is 1_i32", "set count to 2_i64", "info count", "return exit_success"));
    }

    private static void accumulationIsChecked() {
        var source = program(
                "var count is 0_i32",
                "set count to_add count 1_i32",
                "info count",
                "return exit_success");
        expectBody("accumulationIsChecked", """
                  std::int32_t v_count = std::int32_t{0};
                  v_count = runtime::accumulate<std::int32_t>({source, 4, 7}, runtime::Step::add, {v_count, std::int32_t{1}});
                  runtime::info(std::cout, v_count);
                  return EXIT_SUCCESS;
                """, source);
        expect("accumulationIsChecked has its source", Generator.generate(source, "t.bosonoga", "t")
                .contains("constexpr const char *source = \"t.bosonoga\";\n"));
    }

    private static void flagsHaveNoArithmetic() {
        expectError("flagsHaveNoArithmetic",
                "t.bosonoga:4:12: 'bits' is flags, which cannot be used in arithmetic, but set ... to sets it",
                program("var bits is shift_0", "set bits to_add shift_1", "info bits",
                        "return exit_success"));
    }

    private static void accumulationNeedsTheSameKind() {
        expectError("accumulationNeedsTheSameKind",
                "t.bosonoga:4:26: to_add needs i32, but '1d0_f32' is f32",
                program("var count is 0_i32", "set count to_add count 1d0_f32", "info count",
                        "return exit_success"));
    }

    private static void toModNeedsAValueToDivideBy() {
        expectError("toModNeedsAValueToDivideBy", "t.bosonoga:4:13: to_mod needs a value to divide by",
                program("var count is 0_i32", "set count to_mod 3_i32", "info count",
                        "return exit_success"));
    }

    private static void mathNeedsAReal() {
        expectError("mathNeedsAReal", "t.bosonoga:3:28: math_to_sqrt needs f32 or f64, got i32",
                program("var root is math_to_sqrt 4_i32", "info root", "return exit_success"));
    }

    private static void setMustGetItsKind() {
        expectError("setMustGetItsKind",
                "t.bosonoga:4:13: 'count' is i32, but the conversion gives f32",
                program("var count is 0_i32", "set count to_f32 count", "info count",
                        "return exit_success"));
    }

    private static void operationsAreChecked() {
        expectBody("operationsAreChecked", """
                  const double v_real = 0x1.0p1;
                  const double v_root = runtime::math<double>({source, 4, 15}, runtime::Math::pow, v_real, 0x1.0p1);
                  const std::int32_t v_whole = runtime::convert<std::int32_t>({source, 5, 16}, v_root);
                  runtime::info(std::cout, v_whole);
                  return EXIT_SUCCESS;
                """, program(
                "val real is 2d0_f64",
                "val root is math_to_pow real 2d0_f64",
                "val whole is to_i32 root",
                "info whole",
                "return exit_success"));
    }

    private static void comparisonNeedsTheSameKind() {
        expectError("comparisonNeedsTheSameKind", "t.bosonoga:4:8: cannot compare i32 with i64",
                program("val a is 1_i32", "if a lt 1_i64", "di", "  return exit_success", "do",
                        "return exit_failure"));
    }

    private static void realHasNoExactEq() {
        expectError("realHasNoExactEq", "t.bosonoga:4:8: f32 has no exact eq, use eq_3 or eq_6",
                program("val a is 1d0_f32", "if a eq 1d0_f32", "di", "  return exit_success", "do",
                        "return exit_failure"));
    }

    private static void integerHasNoTolerance() {
        expectError("integerHasNoTolerance",
                "t.bosonoga:4:8: eq_3 only works on f32 or f64, i32 uses eq or not_eq",
                program("val a is 1_i32", "if a eq_3 1_i32", "di", "  return exit_success", "do",
                        "return exit_failure"));
    }

    private static void andBindsTighterThanOr() {
        expectBody("andBindsTighterThanOr", """
                  const std::int32_t v_a = std::int32_t{1};
                  if (((v_a < std::int32_t{2}) && (v_a > std::int32_t{0})) || (!(v_a == std::int32_t{1}))) {
                    return EXIT_SUCCESS;
                  }
                  return EXIT_FAILURE;
                """, program(
                "val a is 1_i32",
                "if a lt 2_i32 and a gt 0_i32 or not a eq 1_i32",
                "di",
                "  return exit_success",
                "do",
                "return exit_failure"));
    }

    private static void realIsCloseWithinATolerance() {
        expectBody("realIsCloseWithinATolerance", """
                  const float v_a = 0x1.0p0F;
                  if (!runtime::close(v_a, 0x1.0p0F, 1e-3)) {
                    return EXIT_SUCCESS;
                  }
                  return EXIT_FAILURE;
                """, program(
                "val a is 1d0_f32",
                "if a not_eq_3 1d0_f32",
                "di",
                "  return exit_success",
                "do",
                "return exit_failure"));
    }

    private static void elseIfAndElse() {
        expectBody("elseIfAndElse", """
                  const std::int32_t v_a = std::int32_t{1};
                  if (v_a == std::int32_t{0}) {
                    // nihil
                  } else if (v_a == std::int32_t{1}) {
                    return EXIT_SUCCESS;
                  } else {
                    // nihil
                  }
                  return EXIT_FAILURE;
                """, program(
                "val a is 1_i32",
                "if a eq 0_i32",
                "di",
                "  nihil",
                "do",
                "else if a eq 1_i32",
                "di",
                "  return exit_success",
                "do",
                "else",
                "di",
                "  nihil",
                "do",
                "return exit_failure"));
    }

    private static void endWithoutReturnFails() {
        expectBody("endWithoutReturnFails", """
                  runtime::info(std::cout, std::int32_t{1});
                  std::cerr << "t.bosonoga: bosonoga_main ended without a return\\n";
                  return EXIT_FAILURE;
                """, program("info 1_i32"));
    }

    private static void countedLoopCopiesItsCounter() {
        expectBody("countedLoopCopiesItsCounter", """
                  std::int32_t v_total = std::int32_t{0};
                  for (std::int32_t loop_1 = 0; loop_1 < 3; ++loop_1) {
                    std::int32_t v_index = loop_1;
                    v_total = runtime::accumulate<std::int32_t>({source, 6, 9}, runtime::Step::add, {v_total, v_index});
                  }
                  runtime::info(std::cout, v_total);
                  return EXIT_SUCCESS;
                """, program(
                "var total is 0_i32",
                "to_3 with index",
                "di",
                "  set total to_add total index",
                "do",
                "info total",
                "return exit_success"));
    }

    private static void whileLoopsWithBreakAndContinue() {
        expectBody("whileLoopsWithBreakAndContinue", """
                  bool v_going = true;
                  while (v_going) {
                    v_going = false;
                    continue;
                  }
                  for (std::int32_t loop_1 = 0; loop_1 < 1; ++loop_1) {
                    break;
                  }
                  return EXIT_SUCCESS;
                """, program(
                "var going is true_bool",
                "while going",
                "di",
                "  set going to false_bool",
                "  continue",
                "do",
                "to_1",
                "di",
                "  break",
                "do",
                "return exit_success"));
    }

    private static void breakNeedsALoop() {
        expectError("breakNeedsALoop", "t.bosonoga:3:3: break can only be in a loop",
                program("break", "return exit_success"));
    }

    private static void loopCountFitsIn32Bits() {
        expectError("loopCountFitsIn32Bits", "t.bosonoga:3:3: 'to_2147483648' does not fit in 32 bits",
                program("to_2147483648", "di", "  nihil", "do", "return exit_success"));
    }

    private static void loopVariableMustBeRead() {
        expectError("loopVariableMustBeRead", "t.bosonoga:3:13: 'index' is never read",
                program("to_2 with index", "di", "  nihil", "do", "return exit_success"));
    }

    private static void boolIsACondition() {
        expectBody("boolIsACondition", """
                  const bool v_ready = true;
                  if ((!(v_ready)) || (v_ready)) {
                    return EXIT_SUCCESS;
                  }
                  return EXIT_FAILURE;
                """, program(
                "val ready is true_bool",
                "if not ready or ready",
                "di",
                "  return exit_success",
                "do",
                "return exit_failure"));
    }

    private static void onlyABoolIsACondition() {
        expectError("onlyABoolIsACondition", "t.bosonoga:5:3: expected a comparison, got 'di'",
                program("val count is 1_i32", "if count", "di", "  return exit_success", "do",
                        "return exit_failure"));
    }

    private static void boolComparesOnlyWithEq() {
        expectError("boolComparesOnlyWithEq",
                "t.bosonoga:4:12: bool can only be compared with eq or not_eq",
                program("val ready is true_bool", "if ready gt ready", "di", "  return exit_success",
                        "do", "return exit_failure"));
    }

    private static void stringIsItsText() {
        expectBody("stringIsItsText", """
                  runtime::info(std::cout, std::string{"a\\011b"});
                  return EXIT_SUCCESS;
                """, program("info \"a\tb\"_str", "return exit_success"));
    }

    private static void stringHasNoEscapeSequences() {
        expectError("stringHasNoEscapeSequences", "t.bosonoga:3:10: escape sequences are not supported",
                program("info \"a\\nb\"_str", "return exit_success"));
    }

    private static void stringNeedsItsSuffix() {
        expectError("stringNeedsItsSuffix",
                "t.bosonoga:3:8: a string must have the suffix _str or _xml_str, as in \"text\"_str",
                program("info \"a\"_text", "return exit_success"));
    }

    private static void constantIsWrittenWhereItIsRead() {
        expectBody("constantIsWrittenWhereItIsRead", """
                  runtime::info(std::cout, std::int32_t{3});
                  return EXIT_SUCCESS;
                """, "const limit is 3_i32\n" + program("info limit", "return exit_success"));
    }

    private static void constantMustBeUsed() {
        expectError("constantMustBeUsed", "t.bosonoga:1:7: 'limit' is never used",
                "const limit is 3_i32\n" + program("return exit_success"));
    }

    private static void constantCannotBeSet() {
        expectError("constantCannotBeSet", "t.bosonoga:4:7: 'limit' is a constant and cannot be set",
                "const limit is 3_i32\n" + program("set limit to 4_i32", "return exit_success"));
    }

    private static void constantNameIsTaken() {
        expectError("constantNameIsTaken", "t.bosonoga:4:7: a constant with this name already exists",
                "const limit is 3_i32\n" + program("var limit is 4_i32", "return exit_success"));
    }

    private static void programNeedsMain() {
        expectError("programNeedsMain", "t.bosonoga:2:1: a program needs a bosonoga_main block",
                "const limit is 3_i32\n");
    }

    private static void programHasOneMain() {
        expectError("programHasOneMain",
                "t.bosonoga:5:1: expected const, def, val or end of file, got 'bosonoga_main'",
                program("return exit_success") + program("return exit_success"));
    }

    private static void sleepTakesU32() {
        expectError("sleepTakesU32", "t.bosonoga:3:12: sleep_ms needs u32, but '1_i32' is i32",
                program("sleep_ms 1_i32", "return exit_success"));
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

    // Only what bosonoga_main holds, as the rest is the same for each program.
    private static void expectBody(String name, String expected, String source) {
        String cpp;
        try {
            cpp = Generator.generate(source, "t.bosonoga", "t");
        } catch (BosonogaException exception) {
            failures++;
            System.err.println("FAIL " + name + ": " + exception.getMessage());
            return;
        }
        var start = cpp.indexOf(BODY_START) + BODY_START.length();
        expectEquals(name, expected, cpp.substring(start, cpp.indexOf(BODY_END, start)));
    }

    // A null message accepts any BosonogaException.
    private static void expectError(String name, String message, String source) {
        expectError(name, message, () -> Generator.generate(source, "t.bosonoga", "t"));
    }

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
