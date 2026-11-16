package bosonoga.generator;

import java.nio.file.Path;
import java.util.function.Supplier;

public final class GeneratorTest {
    private static final String DOUBLE_FN =
            "def double_fn value_i32 ret 0_i32\ndi\n  return value_i32\ndo\n";
    private static final String PAIR_FN = "def pair_fn ret 0_i32 1_f32\ndi\n  return 1_i32 2d0_f32\ndo\n";
    private static final String PRIMES = "val primes is array 0_u32\ndi\n  row 2_u32\n  row 3_u32\ndo\n";
    private static final String NEW_READS = "is only read by info, a comparison, new, to_join, io_write, "
            + "an xml_ statement or str_parse, and is not copied";
    private static final String BODY_START = "int bosonoga_main() {\n";
    private static final String BODY_END = "}\n\n}  // namespace\n";

    private static final String META = "[meta]\nname = \"t\"\nversion = \"1\"\n";
    private static final Settings SETTINGS = Settings.parse(META + """
            [color]
            night = "0A0A23"

            [global]
            laps = "3_i32"
            gravity = "-0d5_f64"

            [dir]
            fixture = "test"
            """, "regina.ini", Path.of("."));

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
        functionBecomesACppFunction();
        resultsAreTakenForABlock();
        functionNameEndsWithFn();
        functionMustBeCalled();
        functionMustSayWhatItReturns();
        parameterNameEndsWithItsType();
        resultsAreNumberedInOrder();
        callTakesTheTypesOfItsParameters();
        callGivesItsValuesAfterOf();
        returnHasEachResult();
        functionReturnsNoExitStatus();
        mainReturnsAnExitStatus();
        varTakesOneResult();
        resultIsTakenByItsIndexAndType();
        functionMustBeKnown();
        functionIsNotAValue();
        functionWithoutReturnFails();
        newStrJoinsItsValues();
        nameIsDeclaredAgainAfterUnset();
        newIsNotCopied();
        newIsUnsetInItsBlock();
        newIsUnsetBeforeReturn();
        newIsUnsetBeforeBreak();
        onlyNewIsUnset();
        newCannotBeSet();
        newMakesAStr();
        valArrayBecomesAVector();
        varArrayRowsAreWritten();
        rowsAreLiterals();
        rowHasEachValue();
        topLevelValIsAnArray();
        arrayIsReadAtAnIndex();
        withLenColIsKnownBeforeTheProgramRuns();
        valArrayCannotBeSet();
        lengthIsAnI32();
        colorIsALiteralOfRegina();
        globalIsANumberOfRegina();
        settingIsNotAName();
        missingSettingIsAName();
        settingIsALiteralOfARow();
        colorsCannotBeCompared();
        colorHasNoArithmetic();
        laterSettingIsNotYetAValue();
        globalSectionRunsBeforeMain();
        globalSectionIsFirst();
        globalSectionCannotReturn();
        globalVariableIsReadByALaterSection();
        globalVariableIsReadAtTheEnd();
        functionDoesNotSeeGlobalVariables();
        reginaNeedsMeta();
        reginaIsAKeyFile();
        reginaValueIsUnescaped();
        colorIsSixUppercaseHexDigits();
        globalIsOneNumber();
        gfxSizeIsPositive();
        gfxNeedsABackground();
        keyNamesASokolKey();
        assetNamesAFile();
        reginaMustBeReadable();
        if (failures > 0) {
            System.err.println(failures + " test(s) failed");
            System.exit(1);
        }
    }

    private static void infoIsAFunctionOfTheProgram() {
        var cpp = Generator.generate("// info.bosonoga\n" + program(
                "info 1_i32",
                "info -7_i32 042_i32",
                "return exit_success"), "info.bosonoga", "info", SETTINGS);
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
        expectError("shiftOrNeedsFlags", "t.bosonoga:3:22: expected a statement, got 'shift_or'",
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
        expect("accumulationIsChecked has its source", generate(source)
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
                  runtime::fail({source, 5, 1}, "the program ended without a return");
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

    private static void functionBecomesACppFunction() {
        var source = DOUBLE_FN + program(
                "var result is double_fn of 2_i32 with ret 0_i32",
                "info result",
                "return exit_success");
        expectBody("functionBecomesACppFunction", """
                  std::int32_t v_result = f_double_fn({source, 7, 17}, std::int32_t{2});
                  runtime::info(std::cout, v_result);
                  return EXIT_SUCCESS;
                """, source);
        expect("functionBecomesACppFunction has the function", generate(source)
                .contains("""
                        std::int32_t f_double_fn(const runtime::Where &called_at, std::int32_t v_value_i32) {
                          const runtime::Call call(called_at);
                          return v_value_i32;
                        }
                        """));
    }

    private static void resultsAreTakenForABlock() {
        var source = PAIR_FN + program(
                "call pair_fn with ret 1_f32 as second",
                "di",
                "  info second",
                "do",
                "return exit_success");
        expectBody("resultsAreTakenForABlock", """
                  {
                    const auto result_1 = f_pair_fn({source, 7, 8});
                    float v_second = std::get<1>(result_1);
                    runtime::info(std::cout, v_second);
                  }
                  return EXIT_SUCCESS;
                """, source);
        expect("resultsAreTakenForABlock has the function", generate(source)
                .contains("""
                        std::tuple<std::int32_t, float> f_pair_fn(const runtime::Where &called_at) {
                          const runtime::Call call(called_at);
                          return {std::int32_t{1}, 0x1.0p1F};
                        }
                        """));
    }

    private static void functionNameEndsWithFn() {
        expectError("functionNameEndsWithFn", "t.bosonoga:1:5: the name of a function ends with _fn, as in double_fn",
                "def double value_i32 ret 0_i32\ndi\n  return value_i32\ndo\n" + program("return exit_success"));
    }

    private static void functionMustBeCalled() {
        expectError("functionMustBeCalled", "t.bosonoga:1:5: 'double_fn' is never called",
                DOUBLE_FN + program("return exit_success"));
    }

    private static void functionMustSayWhatItReturns() {
        expectError("functionMustSayWhatItReturns",
                "t.bosonoga:2:1: 'answer_fn' must say what it returns: ret and its types, or ret_void for nothing",
                "def answer_fn\ndi\n  nihil\ndo\n" + program("return exit_success"));
    }

    private static void parameterNameEndsWithItsType() {
        expectError("parameterNameEndsWithItsType",
                "t.bosonoga:1:15: expected a parameter whose name ends with its type, like count_i32, got 'value'",
                "def double_fn value ret 0_i32\ndi\n  return 1_i32\ndo\n" + program("return exit_success"));
    }

    private static void resultsAreNumberedInOrder() {
        expectError("resultsAreNumberedInOrder",
                "t.bosonoga:1:17: the values are numbered from 0 in order, so '1_i32' is 0_i32",
                "def pair_fn ret 1_i32\ndi\n  return 1_i32\ndo\n" + program("return exit_success"));
    }

    private static void callTakesTheTypesOfItsParameters() {
        expectError("callTakesTheTypesOfItsParameters",
                "t.bosonoga:7:30: double_fn needs i32, but '2_i64' is i64",
                DOUBLE_FN + program("var result is double_fn of 2_i64 with ret 0_i32", "info result",
                        "return exit_success"));
    }

    private static void callGivesItsValuesAfterOf() {
        expectError("callGivesItsValuesAfterOf",
                "t.bosonoga:7:27: 'double_fn' takes 1 value, i32, which follow of, as in double_fn of ...",
                DOUBLE_FN + program("var result is double_fn with ret 0_i32", "info result",
                        "return exit_success"));
    }

    private static void returnHasEachResult() {
        expectError("returnHasEachResult",
                "t.bosonoga:3:3: 'pair_fn' returns 2 values, i32 i32, but this return has 1",
                "def pair_fn ret 0_i32 1_i32\ndi\n  return 1_i32\ndo\n" + program("return exit_success"));
    }

    private static void functionReturnsNoExitStatus() {
        expectError("functionReturnsNoExitStatus",
                "t.bosonoga:3:10: only bosonoga_main and gfx_main return an exit status, a function returns its values",
                "def answer_fn ret 0_i32\ndi\n  return exit_success\ndo\n" + program("return exit_success"));
    }

    private static void mainReturnsAnExitStatus() {
        expectError("mainReturnsAnExitStatus", "t.bosonoga:3:10: expected exit_success or exit_failure, got '1_i32'",
                program("return 1_i32"));
    }

    private static void varTakesOneResult() {
        expectError("varTakesOneResult",
                "t.bosonoga:7:7: 'pair_fn' returns 0_i32, 1_f32, which call ... with ret takes for its block, "
                        + "each named, as in with ret 0_i32 as value",
                PAIR_FN + program("var both is pair_fn with ret 0_i32", "info both", "return exit_success"));
    }

    private static void resultIsTakenByItsIndexAndType() {
        expectError("resultIsTakenByItsIndexAndType",
                "t.bosonoga:7:45: 'double_fn' returns i32, so its call ends with ret 0_i32, not '0_i64'",
                DOUBLE_FN + program("var result is double_fn of 2_i32 with ret 0_i64", "info result",
                        "return exit_success"));
    }

    private static void functionMustBeKnown() {
        expectError("functionMustBeKnown", "t.bosonoga:3:8: unknown function 'missing_fn'",
                program("call missing_fn", "return exit_success"));
    }

    private static void functionIsNotAValue() {
        expectError("functionIsNotAValue",
                "t.bosonoga:7:8: 'double_fn' is a function, which is called with var, set ... to or call",
                DOUBLE_FN + program("info double_fn", "return exit_success"));
    }

    private static void functionWithoutReturnFails() {
        expect("functionWithoutReturnFails", generate(
                "def answer_fn ret 0_i32\ndi\n  nihil\ndo\n" + program(
                        "var answer is answer_fn with ret 0_i32", "info answer", "return exit_success"))
                .contains("  runtime::fail({source, 1, 5}, \"the function 'answer_fn' ended without a return\");\n}\n"));
    }

    private static void newStrJoinsItsValues() {
        expectBody("newStrJoinsItsValues", """
                  std::int32_t v_count = std::int32_t{3};
                  std::string v_line = runtime::joined(std::string{"n "}, v_count);
                  runtime::info(std::cout, v_line);
                  runtime::unset(v_line);
                  return EXIT_SUCCESS;
                """, program(
                "var count is 3_i32",
                "val line is new str of \"n \"_str count",
                "info line",
                "unset line",
                "return exit_success"));
    }

    private static void nameIsDeclaredAgainAfterUnset() {
        expectBody("nameIsDeclaredAgainAfterUnset", """
                  std::string v_line = runtime::joined(std::string{"a"});
                  runtime::info(std::cout, v_line);
                  runtime::unset(v_line);
                  std::string v2_line = runtime::joined(std::string{"b"});
                  runtime::info(std::cout, v2_line);
                  runtime::unset(v2_line);
                  return EXIT_SUCCESS;
                """, program(
                "val line is new str of \"a\"_str",
                "info line",
                "unset line",
                "val line is new str of \"b\"_str",
                "info line",
                "unset line",
                "return exit_success"));
    }

    private static void newIsNotCopied() {
        expectError("newIsNotCopied", "t.bosonoga:4:15: 'line' is new, so it " + NEW_READS,
                program("val line is new str of \"a\"_str", "val copy is line", "return exit_success"));
    }

    private static void newIsUnsetInItsBlock() {
        expectError("newIsUnsetInItsBlock", "t.bosonoga:7:3: 'line' is new, and is unset before the end of its block",
                program("if true_bool", "di", "  val line is new str of \"a\"_str", "  info line", "do",
                        "return exit_success"));
    }

    private static void newIsUnsetBeforeReturn() {
        expectError("newIsUnsetBeforeReturn", "t.bosonoga:5:3: unset 'line' before return",
                program("val line is new str of \"a\"_str", "info line", "return exit_success"));
    }

    private static void newIsUnsetBeforeBreak() {
        expectError("newIsUnsetBeforeBreak", "t.bosonoga:7:5: unset 'line' before break",
                program("to_2", "di", "  val line is new str of \"a\"_str", "  info line", "  break", "do",
                        "return exit_success"));
    }

    private static void onlyNewIsUnset() {
        expectError("onlyNewIsUnset", "t.bosonoga:5:9: 'count' is not new, and only what new made is unset",
                program("val count is 1_i32", "info count", "unset count", "return exit_success"));
    }

    private static void newCannotBeSet() {
        expectError("newCannotBeSet",
                "t.bosonoga:4:7: 'line' is new and cannot be set, but it can be unset and declared again",
                program("var line is new str of \"a\"_str", "set line to \"b\"_str", "info line", "unset line",
                        "return exit_success"));
    }

    private static void newMakesAStr() {
        expectError("newMakesAStr",
                "t.bosonoga:3:19: new makes only a str or an xml_str, as in new str of \"A\"_str 1_i32",
                program("val line is new i32 of 1_i32", "return exit_success"));
    }

    private static void valArrayBecomesAVector() {
        var source = PRIMES + program("var second is primes at 1_i32 with ret 0_u32", "info second",
                "return exit_success");
        expectBody("valArrayBecomesAVector", """
                  std::uint32_t v_second = std::get<0>(runtime::row({source, 8, 17}, a_primes, "primes", std::int32_t{1}));
                  runtime::info(std::cout, v_second);
                  return EXIT_SUCCESS;
                """, source);
        expect("valArrayBecomesAVector has the array", generate(source).contains(
                "const std::vector<std::tuple<std::uint32_t>> a_primes = {{std::uint32_t{2U}}, {std::uint32_t{3U}}};\n"));
    }

    private static void varArrayRowsAreWritten() {
        expectBody("varArrayRowsAreWritten", """
                  std::vector<std::tuple<std::int32_t, bool>> v_cells(std::size_t{2});
                  runtime::row({source, 4, 7}, v_cells, "cells", std::int32_t{1}) = {std::int32_t{7}, true};
                  {
                    const auto result_1 = runtime::row({source, 5, 8}, v_cells, "cells", std::int32_t{1});
                    std::int32_t v_seven = std::get<0>(result_1);
                    runtime::info(std::cout, v_seven);
                  }
                  return EXIT_SUCCESS;
                """, program(
                "var cells is array 0_i32 1_bool with_len_col 2_i32",
                "set cells at 1_i32 to 7_i32 true_bool",
                "call cells at 1_i32 with ret 0_i32 as seven",
                "di",
                "  info seven",
                "do",
                "return exit_success"));
    }

    private static void rowsAreLiterals() {
        expectError("rowsAreLiterals",
                "t.bosonoga:3:7: the values of a row are literals, like 7_i32, so a row cannot have 'count'",
                "val counts is array 0_i32\ndi\n  row count\ndo\n" + program("return exit_success"));
    }

    private static void rowHasEachValue() {
        expectError("rowHasEachValue", "t.bosonoga:3:3: 'pairs' has rows of 2 values, i32 i32, but this row has 1",
                "val pairs is array 0_i32 1_i32\ndi\n  row 1_i32\ndo\n" + program("return exit_success"));
    }

    private static void topLevelValIsAnArray() {
        expectError("topLevelValIsAnArray",
                "t.bosonoga:1:14: a top-level val is an array, as in val task is array 0_i32, and const binds a value",
                "val count is 1_i32\n" + program("return exit_success"));
    }

    private static void arrayIsReadAtAnIndex() {
        expectError("arrayIsReadAtAnIndex",
                "t.bosonoga:8:24: 'primes' is an array, which is read at one i32 index, as in primes at 0_i32",
                PRIMES + program("var second is primes with ret 0_u32", "info second", "return exit_success"));
    }

    private static void withLenColIsKnownBeforeTheProgramRuns() {
        expectError("withLenColIsKnownBeforeTheProgramRuns", "t.bosonoga:4:41: with_len_col needs an i32 literal "
                        + "or constant above zero, as the size of an array is known before the program runs",
                program("var size is 2_i32", "var cells is array 0_i32 with_len_col size", "return exit_success"));
    }

    private static void valArrayCannotBeSet() {
        expectError("valArrayCannotBeSet",
                "t.bosonoga:8:7: 'primes' is a val array and cannot be set, but a var array of a block can",
                PRIMES + program("set primes at 0_i32 to 1_u32", "return exit_success"));
    }

    private static void lengthIsAnI32() {
        expectBody("lengthIsAnI32", """
                  runtime::info(std::cout, std::int32_t{2});
                  return EXIT_SUCCESS;
                """, PRIMES + "const count is len_col primes\n" + program("info count", "return exit_success"));
    }

    private static void colorIsALiteralOfRegina() {
        expectBody("colorIsALiteralOfRegina", """
                  const runtime::Color v_sky = runtime::Color{0x0A0A23U};
                  runtime::info(std::cout, v_sky);
                  return EXIT_SUCCESS;
                """, program("val sky is color_night", "info sky", "return exit_success"));
    }

    private static void globalIsANumberOfRegina() {
        expectBody("globalIsANumberOfRegina", """
                  std::vector<std::tuple<std::int32_t>> v_cells(std::size_t{3});
                  runtime::info(std::cout, std::int32_t{3}, -0x1.0p-1);
                  const std::int32_t v_count = std::int32_t{3};
                  runtime::info(std::cout, v_count);
                  return EXIT_SUCCESS;
                """, program(
                "var cells is array 0_i32 with_len_col global_laps",
                "info global_laps global_gravity",
                "val count is len_col cells",
                "info count",
                "return exit_success"));
    }

    private static void settingIsNotAName() {
        expectError("settingIsNotAName", "t.bosonoga:3:7: expected a variable name, got 'color_night'",
                program("var color_night is 1_i32", "return exit_success"));
        expectError("settingIsNotAName of const", "t.bosonoga:1:7: expected a variable name, got 'global_laps'",
                "const global_laps is 1_i32\n" + program("return exit_success"));
        expectError("settingIsNotAName of set", "t.bosonoga:3:7: expected a variable name, got 'color_night'",
                program("set color_night to color_night", "return exit_success"));
    }

    private static void missingSettingIsAName() {
        expectError("missingSettingIsAName", "t.bosonoga:3:8: unknown variable 'color_day'",
                program("info color_day", "return exit_success"));
        expectBody("missingSettingIsAName declared", """
                  const std::int32_t v_global_day = std::int32_t{1};
                  runtime::info(std::cout, v_global_day);
                  return EXIT_SUCCESS;
                """, program("val global_day is 1_i32", "info global_day", "return exit_success"));
    }

    private static void settingIsALiteralOfARow() {
        expect("settingIsALiteralOfARow", generate("val tints is array 0_color 1_i32\ndi\n"
                + "  row color_night global_laps\ndo\n" + program(
                "call tints at 0_i32 with ret 0_color as tint 1_i32 as laps",
                "di",
                "  info tint laps",
                "do",
                "return exit_success"))
                .contains("const std::vector<std::tuple<runtime::Color, std::int32_t>> a_tints = "
                        + "{{runtime::Color{0x0A0A23U}, std::int32_t{3}}};\n"));
    }

    private static void colorsCannotBeCompared() {
        expectError("colorsCannotBeCompared", "t.bosonoga:3:18: colors cannot be compared", program(
                "if color_night eq color_night",
                "di",
                "  return exit_success",
                "do",
                "return exit_failure"));
    }

    private static void colorHasNoArithmetic() {
        expectError("colorHasNoArithmetic",
                "t.bosonoga:4:12: 'tint' is color, which cannot be used in arithmetic, but set ... to sets it",
                program("var tint is color_night", "set tint to_add tint", "return exit_success"));
        expectError("colorHasNoArithmetic of to_i32", "t.bosonoga:3:24: to_i32 needs a number, got color",
                program("var number is to_i32 color_night", "return exit_success"));
    }

    private static void laterSettingIsNotYetAValue() {
        expectError("laterSettingIsNotYetAValue", "t.bosonoga:3:8: a value of dir is not supported yet",
                program("info dir_fixture", "return exit_success"));
    }

    private static void globalSectionRunsBeforeMain() {
        expectBody("globalSectionRunsBeforeMain", """
                  std::int32_t v_laps = std::int32_t{3};
                  v_laps = runtime::accumulate<std::int32_t>({source, 8, 7}, runtime::Step::add, {v_laps, std::int32_t{1}});
                  runtime::info(std::cout, v_laps);
                  return EXIT_SUCCESS;
                """,
                "bosonoga_global\ndi\n  var laps is global_laps\ndo\n\n"
                + program("set laps to_add laps 1_i32", "info laps", "return exit_success"));
    }

    private static void globalSectionIsFirst() {
        expectError("globalSectionIsFirst", "t.bosonoga:5:1: bosonoga_global must be the first section",
                program("return exit_success") + "bosonoga_global\ndi\n  nihil\ndo\n");
        expectError("globalSectionIsFirst twice", "t.bosonoga:5:1: bosonoga_global must be the first section",
                "bosonoga_global\ndi\n  nihil\ndo\nbosonoga_global\ndi\n  nihil\ndo\n"
                        + program("return exit_success"));
    }

    private static void globalSectionCannotReturn() {
        expectError("globalSectionCannotReturn", "t.bosonoga:3:3: only bosonoga_main and gfx_main can return",
                "bosonoga_global\ndi\n  return exit_success\ndo\n" + program("return exit_success"));
    }

    private static void globalVariableIsReadByALaterSection() {
        expectError("globalVariableIsReadByALaterSection", "t.bosonoga:7:7: variable 'laps' already exists",
                "bosonoga_global\ndi\n  var laps is 1_i32\ndo\n" + program(
                        "var laps is 2_i32", "info laps", "return exit_success"));
        expectError("globalVariableIsReadByALaterSection of const", "t.bosonoga:5:7: variable 'laps' already exists",
                "bosonoga_global\ndi\n  var laps is 1_i32\ndo\nconst laps is 1_i32\n" + program(
                        "info laps", "return exit_success"));
        expectError("globalVariableIsReadByALaterSection of new",
                "t.bosonoga:4:1: 'text' is new, and is unset before the end of its block",
                "bosonoga_global\ndi\n  var text is new str of 1_i32\ndo\n" + program("return exit_success"));
    }

    private static void globalVariableIsReadAtTheEnd() {
        expectError("globalVariableIsReadAtTheEnd", "t.bosonoga:3:7: 'laps' is never read",
                "bosonoga_global\ndi\n  var laps is 1_i32\ndo\n" + program("var other is 1_i32", "info other",
                        "return exit_success"));
        expectError("globalVariableIsReadAtTheEnd of a block", "t.bosonoga:5:9: 'lap' is never read",
                "bosonoga_global\ndi\n  if true_bool\n  di\n    var lap is 1_i32\n  do\ndo\n"
                        + program("return exit_success"));
    }

    private static void functionDoesNotSeeGlobalVariables() {
        expectError("functionDoesNotSeeGlobalVariables", "t.bosonoga:7:8: unknown variable 'laps'",
                "bosonoga_global\ndi\n  var laps is 1_i32\ndo\ndef show_fn ret_void\ndi\n  info laps\ndo\n"
                        + program("call show_fn", "info laps", "return exit_success"));
        expect("functionDoesNotSeeGlobalVariables has its own names", generate(
                "bosonoga_global\ndi\n  var laps is 1_i32\ndo\ndef show_fn laps_i32 ret_void\ndi\n"
                        + "  info laps_i32\ndo\n" + program("call show_fn of laps", "return exit_success"))
                .contains("void f_show_fn(const runtime::Where &called_at, std::int32_t v_laps_i32) {\n"));
    }

    private static void reginaNeedsMeta() {
        expectSettingsError("reginaNeedsMeta", "regina.ini: Key file does not have group “meta”", "");
        expectSettingsError("reginaNeedsMeta version", "regina.ini: Key file does not have key “version” in "
                + "group “meta”", "[meta]\nname = \"t\"\n");
        expect("reginaNeedsMeta without quotes", Settings.parse("[meta]\nname = t\nversion = \"1\"\n",
                "regina.ini", Path.of(".")).name.equals("t"));
    }

    private static void reginaIsAKeyFile() {
        expectSettingsError("reginaIsAKeyFile", "regina.ini: Key file does not start with a group", "name = \"t\"\n");
        expectSettingsError("reginaIsAKeyFile of a line", "regina.ini: Key file contains line “  junk” which is "
                + "not a key-value pair, group, or comment", META + "  junk\n");
        expectSettingsError("reginaIsAKeyFile of a group", "regina.ini: Invalid group name: bad[group",
                META + "[bad[group]\n");
        expectSettingsError("reginaIsAKeyFile of a key", "regina.ini: Invalid key name: c]",
                META + "[color]\nc] = \"ABCDEF\"\n");
        var settings = Settings.parse(META.replace("\n", "\r\n") + "# a comment\n  \n[color]\n  c\t=\t\"000001\"\n"
                + "c = \"000002\"\nc[de] = \"000003\"\n", "regina.ini", Path.of("."));
        expect("reginaIsAKeyFile keeps the last value", settings.colors.equals(java.util.Map.of("c", 2)));
    }

    private static void reginaValueIsUnescaped() {
        expect("reginaValueIsUnescaped", Settings.parse("[meta]\nname = \"a\\sb\\\\\"\nversion = \"1\"\n",
                "regina.ini", Path.of(".")).name.equals("a b\\"));
        expectSettingsError("reginaValueIsUnescaped of an escape",
                "regina.ini: Key file contains key “c” which has a value that cannot be interpreted.",
                META + "[color]\nc = \"ABC\\qDE\"\n");
    }

    private static void colorIsSixUppercaseHexDigits() {
        expectSettingsError("colorIsSixUppercaseHexDigits", "regina.ini: c must be 6 uppercase hex digits",
                META + "[color]\nc = \"abcdef\"\n");
        // GKeyFile keeps the spaces after a value, and then its quotes.
        expectSettingsError("colorIsSixUppercaseHexDigits with a space", "regina.ini: c must be 6 uppercase hex "
                + "digits", META + "[color]\nc = \"ABCDEF\" \n");
    }

    private static void globalIsOneNumber() {
        for (var text : new String[] {"\"5\"", "\"5_i32 6_i32\"", "\"3000000000_i32\"", "\"true_bool\"",
                "\"1d_f32\"", "\"\""}) {
            expectSettingsError("globalIsOneNumber " + text,
                    "regina.ini: g must be one number, like 100_i32 or 100d0_f32", META + "[global]\ng = " + text + "\n");
        }
        var globals = Settings.parse(META + "[global]\na = 7_u64\nb = \"01_i32 // laps\"\n", "regina.ini",
                Path.of(".")).globals;
        expect("globalIsOneNumber of a number", globals.get("a").cpp().equals("std::uint64_t{7ULL}")
                && globals.get("b").cpp().equals("std::int32_t{1}"));
    }

    private static void gfxSizeIsPositive() {
        expectSettingsError("gfxSizeIsPositive", "regina.ini: height must be positive",
                META + "[gfx]\nwidth = +1\nheight = -1\n");
        for (var text : new String[] {"\"640\"", "0x10", "2147483648", "64x", ""}) {
            expectSettingsError("gfxSizeIsPositive " + text, "regina.ini: Key file contains key “width” in group "
                    + "“gfx” which has a value that cannot be interpreted.", META + "[gfx]\nwidth = " + text
                    + "\nheight = 1\n");
        }
        expect("gfxSizeIsPositive as strtol", Settings.parse(META + "[gfx]\nwidth = 64 x\nheight = 48\n",
                "regina.ini", Path.of(".")).gfxWidth == 64);
    }

    private static void gfxNeedsABackground() {
        expectSettingsError("gfxNeedsABackground", "regina.ini: the group color needs the key background",
                META + "[color]\nc = \"ABCDEF\"\n[gfx]\nwidth = 1\nheight = 1\n");
        expect("gfxNeedsABackground only with colors", Settings.parse(META + "[gfx]\nwidth = 1\nheight = 1\n",
                "regina.ini", Path.of(".")).backgroundColor == 0);
    }

    private static void keyNamesASokolKey() {
        expectSettingsError("keyNamesASokolKey", "regina.ini: j must name a key, like SAPP_KEYCODE_W, "
                + "SAPP_KEYCODE_0 or SAPP_KEYCODE_ESCAPE", META + "[key]\nk = \"SAPP_KEYCODE_W\"\nj = \"W\"\n");
    }

    private static void assetNamesAFile() {
        expectSettingsError("assetNamesAFile", "regina.ini: d must name a directory, like \"replay\"",
                META + "[dir]\nd = \"\"\n");
        expectSettingsError("assetNamesAFile of a font", "regina.ini: f must name a font file, like "
                + "\"asset/font/IntelOneMono-Medium.ttf\"", META + "[font]\nf = \"\"\n");
        var dirs = Settings.parse(META + "[dir]\nd = \"replay\"\ne = \"/tmp\"\n", "regina.ini", Path.of(".")).dirs;
        expect("assetNamesAFile from the directory of regina.ini", dirs.get("d").path().equals("./replay")
                && dirs.get("e").path().equals("/tmp"));
    }

    private static void reginaMustBeReadable() {
        expectError("reginaMustBeReadable", "build/no_such_directory/regina.ini: No such file or directory", () -> {
            Settings.read(Path.of("build/no_such_directory/regina.ini"));
            return "";
        });
    }

    // Lines start at line 3 and column 3, after "bosonoga_main", "di" and the indent.
    private static String program(String... lines) {
        var source = new StringBuilder("bosonoga_main\ndi\n");
        for (var line : lines) {
            source.append("  ").append(line).append('\n');
        }
        return source.append("do\n").toString();
    }

    private static String generate(String source) {
        return Generator.generate(source, "t.bosonoga", "t", SETTINGS);
    }

    private static void expectSettingsError(String name, String message, String text) {
        expectError(name, message, () -> {
            Settings.parse(text, "regina.ini", Path.of("."));
            return "";
        });
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
            cpp = generate(source);
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
        expectError(name, message, () -> generate(source));
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
