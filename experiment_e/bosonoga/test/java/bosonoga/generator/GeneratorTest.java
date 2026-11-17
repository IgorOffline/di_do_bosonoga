package bosonoga.generator;

import java.io.IOException;
import java.nio.file.Files;
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

            [key]
            jump = "SAPP_KEYCODE_SPACE"

            [font]
            mono = "asset/font/m.ttf"

            [dir]
            fixture = "test"
            """, "regina.ini", Path.of("."));

    private static int failures;

    private GeneratorTest() {
    }

    public static void main(String[] args) {
        failures += GfxGeneratorTest.run();
        infoIsAFunctionOfTheProgram();
        numbersOfEveryKind();
        numberMustFitItsKind();
        unsignedIsNotNegative();
        numberNeedsASuffix();
        integerHasNoFraction();
        f32MustFit();
        infoNeedsAValue();
        xmlStrIsWellFormedXml();
        newXmlStrJoinsAnElement();
        newXmlStrNamesAreLiterals();
        toJoinJoinsToAVarOfNewXmlStr();
        xmlChildGivesABlock();
        givenTextIsFreedAfterItsBlock();
        strParseTakesItsNumber();
        strAndXmlStrCompareByText();
        blockNeedsAStatement();
        blockNestingMatchesTheArchive();
        shiftOrAndShiftHasBecomeBitOperations();
        shiftBitIsAtMost31();
        shiftOrNeedsFlags();
        shiftHasNeedsFlags();
        nameMustBeDeclared();
        signatureDiagnosticsUseBoundedTokens();
        xmlConstructionDiagnosticsUseBoundedTokens();
        mutationDiagnosticsUseBoundedNames();
        indexedTypeMarkersAreNotValues();
        typedOperandsUseBoundedDisplayText();
        callDiagnosticsUseBoundedCalleeNames();
        unusedSymbolNamesUseTheArchiveDisplayLimit();
        hiddenSymbolNamesUseTheArchiveDisplayLimit();
        declarationNamesUseTheArchiveDisplayLimit();
        arrayIndicesAreCheckedBeforeExtraIndices();
        arrayAssignmentErrorsDescribeRows();
        loopCounterNamesAreCheckedBeforeBodies();
        valueNameDiagnosticsUseTheArchiveDisplayLimit();
        filenamesHaveSafeCppExports();
        sourceListsPreserveWholePaths();
        importedSourcesNeedNoModuleSuffix();
        selectedProgramsAreAutomaticallyRegistered();
        strParseChecksItsNumberBeforeItsBlock();
        voidCallsRejectBareBlocks();
        reservedParametersAreCheckedAfterTheirType();
        resultBindingNamesAreCheckedInOrder();
        topLevelArraysUseVariableNameRules();
        assignmentsCheckTheirSourceBeforeTheirShape();
        constantsCannotCaptureGlobalVariables();
        declarationNamesAreCheckedBeforeInitializers();
        reservedResultNamesMatchTheArchive();
        unknownFunctionValuesAreFunctions();
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
        asciiWhitespaceAndCommentsMatchTheArchive();
        carriageReturnsAreStringContent();
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
        reginaNamesValues();
        reginaValuesCompareOnlyEqually();
        reginaValueHasNoZero();
        ioReadGivesABlock();
        ioWriteWritesText();
        ioReasonIsFreedAfterItsBlock();
        fileNameIsBuiltAsGLibDoes();
        moduleSeesWhatItsReaderSees();
        pubIsOnlyInAModule();
        modReadsAFileBesideTheProgram();
        moduleNamesItsFileInErrors();
        rawImportsContinueInTheirReader();
        importFilenamesContinueAcrossRawEof();
        blockSaysWhatItExpects();
        topLevelSaysWhatItExpects();
        stringIsCheckedAsItIsRead();
        numberIsCheckedAsItIsRead();
        errorsComeInTheOrderOfTheProgram();
        syntaxSaysWhatItExpects();
        callSaysWhatItTakes();
        pubVariableIsOfAModule();
        columnCountsBytes();
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

    private static void xmlStrIsWellFormedXml() {
        expectBody("xmlStrIsWellFormedXml", """
                  runtime::info(std::cout, std::string{"<a b='1'>x &amp; y</a>"});
                  return EXIT_SUCCESS;
                """, program("info \"<a b='1'>x &amp; y</a>\"_xml_str", "return exit_success"));
        var problems = new String[][] {
                {"<a>", "'a' is not closed"},
                {"</a>", "an element is closed that was not opened"},
                {"<a></b>", "Element “b” was closed, but the currently open element is “a”"},
                {"a & b", "Entity did not end with a semicolon; most likely you used an ampersand character without "
                        + "intending to start an entity — escape ampersand as &amp;"},
                {"&#0;", "Character reference “0” does not encode a permitted character"},
                {"<a b='1' b='2'/>", "'a' has the attribute 'b' twice"},
                {"<a b='<'/>", "an attribute has a <, which it writes as &lt;"},
                {"<!-- c -->", "an xml_str has no comment, CDATA, processing instruction or doctype, but '<!-- c -->'"},
                {"<1a/>", "“1a” is not a valid name"},
                {"a < b", "“ ” is not a valid character following a “<” character; it may not begin an element name"},
        };
        for (var problem : problems) {
            expectError("xmlStrIsWellFormedXml " + problem[0], "t.bosonoga:3:8: an xml_str is well-formed XML, but "
                    + problem[1], program("info \"" + problem[0] + "\"_xml_str", "return exit_success"));
        }
    }

    private static void newXmlStrJoinsAnElement() {
        expectBody("newXmlStrJoinsAnElement", """
                  std::int32_t v_count = std::int32_t{2};
                  std::string v_shelf = runtime::XmlJoin().open({source, 4, 28}, "shelf").name({source, 4, 48}, "count").value({source, 4, 60}, v_count).start({source, 4, 28}).raw({source, 4, 69}, std::string{"<a/>"}).text({source, 4, 84}, std::string{"&"}).close({source, 4, 28}, "shelf").take();
                  std::string v_empty = runtime::XmlJoin().open({source, 5, 28}, "empty").empty({source, 5, 28}).take();
                  std::string v_notes = runtime::XmlJoin().text({source, 6, 31}, v_count).take();
                  v_notes = runtime::XmlJoin(v_notes).raw({source, 7, 21}, v_empty).take();
                  runtime::info(std::cout, v_shelf, v_empty, v_notes);
                  runtime::unset(v_shelf);
                  runtime::unset(v_empty);
                  runtime::unset(v_notes);
                  return EXIT_SUCCESS;
                """, program(
                "var count is 2_i32",
                "var shelf is new xml_str element \"shelf\"_str \"count\"_str count of \"<a/>\"_xml_str \"&\"_str",
                "var empty is new xml_str element \"empty\"_str",
                "var notes is new xml_str of count",
                "set notes to_join empty",
                "info shelf empty notes",
                "unset shelf",
                "unset empty",
                "unset notes",
                "return exit_success"));
    }

    private static void newXmlStrNamesAreLiterals() {
        expectError("newXmlStrNamesAreLiterals", "t.bosonoga:4:34: expected the name of an element, a str literal "
                + "like \"action\"_str, got 'name'", program("var name is \"a\"_str",
                "var doc is new xml_str element name", "return exit_success"));
        expectError("newXmlStrNamesAreLiterals of a name", "t.bosonoga:3:34: \"1a\"_str is not the name of an "
                + "element, which starts with an ASCII letter or _, and has letters, digits, _, - and .",
                program("var doc is new xml_str element \"1a\"_str", "return exit_success"));
        expectError("newXmlStrNamesAreLiterals given twice", "t.bosonoga:3:57: the attribute \"id\"_str is already "
                + "given", program("var doc is new xml_str element \"a\"_str \"id\"_str 1_i32 \"id\"_str 2_i32",
                "return exit_success"));
        expectError("newXmlStrNamesAreLiterals of an attribute", "t.bosonoga:3:57: expected the name of an "
                + "attribute, a str literal like \"action\"_str, got '7_i32'", program(
                "var doc is new xml_str element \"a\"_str \"id\"_str 1_i32 7_i32", "return exit_success"));
        expectError("newXmlStrNamesAreLiterals without a value", "t.bosonoga:4:3: expected the value of the "
                + "attribute, got 'return'", program("var doc is new xml_str element \"a\"_str \"id\"_str",
                "return exit_success"));
        expectError("newXmlStrNamesAreLiterals without element", "t.bosonoga:4:3: expected element or of, as in "
                + "new xml_str element \"action\"_str, got 'return'", program("var doc is new xml_str",
                "return exit_success"));
    }

    private static void toJoinJoinsToAVarOfNewXmlStr() {
        expectError("toJoinJoinsToAVarOfNewXmlStr of a val", "t.bosonoga:4:7: 'doc' is a val, and only a var of "
                + "new xml_str is joined to", program("val doc is new xml_str of \"a\"_str",
                "set doc to_join \"b\"_str", "unset doc", "return exit_success"));
        expectError("toJoinJoinsToAVarOfNewXmlStr of to", "t.bosonoga:4:11: 'doc' is new, so it is only joined to, "
                + "as in set doc to_join", program("var doc is new xml_str of \"a\"_str",
                "set doc to \"b\"_xml_str", "unset doc", "return exit_success"));
        expectError("toJoinJoinsToAVarOfNewXmlStr of an i32", "t.bosonoga:4:9: 'n' is i32, but only a var of new "
                + "xml_str is joined to", program("var n is 1_i32", "set n to_join \"b\"_str", "info n",
                "return exit_success"));
        expectError("toJoinJoinsToAVarOfNewXmlStr is not reading", "t.bosonoga:3:7: 'doc' is never read",
                program("var doc is new xml_str of \"a\"_str", "set doc to_join \"b\"_str", "unset doc",
                        "return exit_success"));
    }

    private static void xmlChildGivesABlock() {
        expectBody("xmlChildGivesABlock", """
                  {
                    const auto result_1 = runtime::xml_child({source, 3, 3}, std::string{"<a><b/></a>"}, "b", std::int32_t{0});
                    bool v_found = std::get<0>(result_1);
                    const std::string v_child = std::get<1>(result_1);
                    runtime::info(std::cout, v_found, v_child);
                  }
                  return EXIT_SUCCESS;
                """, program(
                "xml_child \"<a><b/></a>\"_xml_str \"b\"_str 0_i32 with ret 0_bool as found 1_xml_str as child",
                "di",
                "  info found child",
                "do",
                "return exit_success"));
        expectError("xmlChildGivesABlock without with", "t.bosonoga:4:3: xml_child gives 0_bool, 1_xml_str, which "
                + "with ret takes for a block, each named, as in with ret 0_bool as done", program(
                "xml_child \"<a/>\"_xml_str \"x\"_str 0_i32", "return exit_success"));
        expectError("xmlChildGivesABlock of a value", "t.bosonoga:3:36: expected a value, got 'with'", program(
                "xml_child \"<a/>\"_xml_str \"x\"_str with ret 0_bool as found", "di", "  info found", "do",
                "return exit_success"));
        expectError("xmlChildGivesABlock of a type", "t.bosonoga:3:67: 'xml_child' gives 0_bool, 1_xml_str, so it "
                + "has no 2_i32", program("xml_child \"<a/>\"_xml_str \"x\"_str 0_i32 with ret 0_bool as found "
                + "2_i32 as x", "di", "  info found", "do", "return exit_success"));
    }

    private static void givenTextIsFreedAfterItsBlock() {
        var child = "xml_child \"<a><b/></a>\"_xml_str \"b\"_str 0_i32 with ret 1_xml_str as child";
        expectError("givenTextIsFreedAfterItsBlock of unset", "t.bosonoga:5:11: 'child' is freed after the block "
                + "that xml_child gives it, and is not unset", program(child, "di", "  unset child", "do",
                "return exit_success"));
        expectError("givenTextIsFreedAfterItsBlock of a copy", "t.bosonoga:5:17: 'child' is taken from a block, so "
                + "it " + NEW_READS, program(child, "di", "  var copy is child", "  info copy", "do",
                "return exit_success"));
        expectError("givenTextIsFreedAfterItsBlock of return", "t.bosonoga:6:5: return cannot leave the block that "
                + "xml_child gives 'child'", program(child, "di", "  info child", "  return exit_success", "do",
                "return exit_success"));
        expectError("givenTextIsFreedAfterItsBlock of break", "t.bosonoga:8:7: break cannot leave the block that "
                + "xml_child gives 'child'", program("to_2", "di", "  " + child, "  di", "    info child",
                "    break", "  do", "do", "return exit_success"));
        expectError("givenTextIsFreedAfterItsBlock of set", "t.bosonoga:5:9: 'child' is taken from a block and "
                + "cannot be set, but it can be unset and declared again", program(child, "di",
                "  set child to_join \"x\"_xml_str", "do", "return exit_success"));
    }

    private static void strParseTakesItsNumber() {
        expectBody("strParseTakesItsNumber", """
                  {
                    const auto result_1 = runtime::str_parse<std::uint64_t>(std::string{"7"});
                    std::uint64_t v_number = std::get<1>(result_1);
                    runtime::info(std::cout, v_number);
                  }
                  return EXIT_SUCCESS;
                """, program("str_parse \"7\"_str with ret 1_u64 as number", "di", "  info number", "do",
                "return exit_success"));
        expectError("strParseTakesItsNumber without it", "t.bosonoga:3:3: str_parse takes its number, whose type it "
                + "parses, as in with ret 0_bool as parsed 1_i32 as number", program(
                "str_parse \"1\"_str with ret 0_bool as parsed", "di", "  info parsed", "do", "return exit_success"));
        expectError("strParseTakesItsNumber of a type", "t.bosonoga:3:47: 'str_parse' gives 0_bool, 1_i32, so it "
                + "has no 1_f32", program("str_parse \"1\"_str with ret 0_bool as parsed 1_f32 as number", "di",
                "  info parsed", "do", "return exit_success"));
    }

    private static void strAndXmlStrCompareByText() {
        expectBody("strAndXmlStrCompareByText", """
                  if (std::string{"<a/>"} == std::string{"<a/>"}) {
                    return EXIT_SUCCESS;
                  }
                  return EXIT_FAILURE;
                """, program("if \"<a/>\"_xml_str eq \"<a/>\"_str", "di", "  return exit_success", "do",
                "return exit_failure"));
        expectError("strAndXmlStrCompareByText only with eq", "t.bosonoga:3:21: xml_str can only be compared with "
                + "eq or not_eq", program("if \"<a/>\"_xml_str gt \"<a/>\"_str", "di", "  return exit_success",
                "do", "return exit_failure"));
    }

    private static void blockNeedsAStatement() {
        expectError("blockNeedsAStatement", null, program());
    }

    private static void blockNestingMatchesTheArchive() {
        var nested = "if true_bool di\n";
        var close = "do\n";
        var accepted = "bosonoga_main\ndi\n" + nested.repeat(255) + "nihil\n" + close.repeat(255)
                + "return exit_success\ndo\n";
        expect("256 blocks are accepted", generate(accepted).contains("return EXIT_SUCCESS;"));
        var prefix = "bosonoga_main\ndi\n" + nested.repeat(255);
        expectError("257 blocks are rejected", "t.bosonoga:258:14: blocks are nested more than 256 levels deep",
                prefix + nested + "nihil\n" + close.repeat(256) + "return exit_success\ndo\n");
        expectError("empty block precedes nesting limit",
                "t.bosonoga:258:14: empty block, write nihil for a block that does nothing",
                prefix + "if true_bool di do\n" + close.repeat(255) + "return exit_success\ndo\n");
        expectError("earlier semantic error precedes nesting limit", "t.bosonoga:3:6: unknown variable 'missing'",
                "bosonoga_main\ndi\ninfo missing\n" + nested.repeat(256) + "nihil\n" + close.repeat(257));
        expectError("very deep input is bounded", "t.bosonoga:258:14: blocks are nested more than 256 levels deep",
                "bosonoga_main\ndi\n" + nested.repeat(10000) + "nihil\n" + close.repeat(10001));
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
        expectError("toModNeedsAValueToDivideBy", "t.bosonoga:5:3: expected a value to divide by, got 'info'",
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

    private static void asciiWhitespaceAndCommentsMatchTheArchive() {
        expectBody("all ASCII whitespace separates tokens", "  return EXIT_SUCCESS;\n",
                "bosonoga_main\u000bdi\freturn\texit_success\r\ndo\n");
        expectBody("only LF ends a comment", "  return EXIT_SUCCESS;\n",
                "// ignored\rthis is still a comment\u000b\f\n" + program("return exit_success"));
        expectError("control whitespace preserves byte columns",
                "t.bosonoga:3:8: unknown variable 'missing'",
                "bosonoga_main\ndi\n\u000b\finfo missing\ndo\n");
        expectError("non-ASCII whitespace remains invalid", "t.bosonoga:3:1: unexpected character",
                "bosonoga_main\ndi\n\u00a0return exit_success\ndo\n");
    }

    private static void carriageReturnsAreStringContent() {
        expectBody("CR is escaped only in emitted C++", """
                  runtime::info(std::cout, std::string{"a\\015b"});
                  return EXIT_SUCCESS;
                """, program("info \"a\rb\"_str", "return exit_success"));
        expectError("LF still terminates a string", "t.bosonoga:3:8: unterminated string",
                program("info \"a\r\nb\"_str", "return exit_success"));
        expectError("CR does not start a new diagnostic line",
                "t.bosonoga:3:18: unknown variable 'missing'",
                program("info \"a\rb\"_str missing", "return exit_success"));
        expectError("backslash before CR remains an escape error",
                "t.bosonoga:3:10: escape sequences are not supported",
                program("info \"a\\\rb\"_str", "return exit_success"));
    }

    private static void signatureDiagnosticsUseBoundedTokens() {
        var prefix = "abcdefghijklmnopqrstuvwxyzabcdef";
        expectError("function-name suggestion is bounded", "t.bosonoga:1:5: the name of a function ends with _fn, "
                        + "as in " + prefix + "_fn",
                "def " + prefix + "_name ret_void di nihil do\n" + program("return exit_success"));
        expectError("legacy fn prefix is removed after display truncation",
                "t.bosonoga:1:5: the name of a function ends with _fn, as in abcdefghijklmnopqrstuvwxyzabc_fn",
                "def fn_" + prefix + "_name ret_void di nihil do\n" + program("return exit_success"));
        expectError("invalid parameter token display is bounded",
                "t.bosonoga:1:13: expected a parameter whose name ends with its type, like count_i32, got '" + prefix + "'",
                "def echo_fn " + prefix + "_bad ret_void di nihil do\n" + program("return exit_success"));
        expectError("missing return declaration uses bounded function name", "t.bosonoga:1:41: '" + prefix
                        + "' must say what it returns: ret and its types, or ret_void for nothing",
                "def " + prefix + "_fn di nihil do\n" + program("return exit_success"));
    }

    private static void xmlConstructionDiagnosticsUseBoundedTokens() {
        var prefix = "abcdefghijklmnopqrstuvwxyzabcdef";
        var shown = "\"abcdefghijklmnopqrstuvwxyzabcde";
        var invalid = "var xml is new xml_str element \"" + prefix + ":\"_str";
        expectError("invalid XML name display is bounded", "t.bosonoga:3:34: " + shown
                        + " is not the name of an element, which starts with an ASCII letter or _, and has letters, digits, _, - and .",
                program(invalid, "info xml", "unset xml", "return exit_success"));
        var attribute = prefix + "_attribute";
        var duplicate = "var xml is new xml_str element \"a\"_str \"" + attribute + "\"_str 1_i32 \"" + attribute + "\"_str 2_i32";
        var column = duplicate.lastIndexOf("\"" + attribute) + 3;
        expectError("duplicate XML attribute display is bounded", "t.bosonoga:3:" + column
                        + ": the attribute " + shown + " is already given",
                program(duplicate, "info xml", "unset xml", "return exit_success"));
        expect("XML attribute identity uses full names", generate(program(
                "var xml is new xml_str element \"a\"_str \"" + prefix + "_one\"_str 1_i32 \""
                        + prefix + "_two\"_str 2_i32", "info xml", "unset xml", "return exit_success"))
                .contains(prefix + "_two"));
    }

    private static void mutationDiagnosticsUseBoundedNames() {
        var prefix = "abcdefghijklmnopqrstuvwxyzabcdef";
        var name = prefix + "_value";
        expectError("long constant is recognized and cannot be set",
                "t.bosonoga:4:7: '" + prefix + "' is a constant and cannot be set",
                "const " + name + " is 1_i32\n" + program("set " + name + " to 2_i32", "return exit_success"));
        expectError("val mutation display is bounded", "t.bosonoga:4:7: '" + prefix + "' is a val and cannot be set",
                program("val " + name + " is 1_i32", "set " + name + " to 2_i32", "return exit_success"));
        expectError("unset eligibility display is bounded", "t.bosonoga:4:9: '" + prefix
                        + "' is not new, and only what new made is unset",
                program("var " + name + " is 1_i32", "unset " + name, "return exit_success"));
        expectError("owned mutation display is bounded", "t.bosonoga:4:7: '" + prefix
                        + "' is new and cannot be set, but it can be unset and declared again",
                program("var " + name + " is new str of \"a\"_str", "set " + name + " to \"b\"_str",
                        "return exit_success"));
        expect("unset still removes the complete name", generate(program(
                "var " + name + " is new str of \"a\"_str", "info " + name, "unset " + name,
                "var " + name + " is 1_i32", "info " + name, "return exit_success"))
                .contains("v2_" + name));
    }

    private static void indexedTypeMarkersAreNotValues() {
        for (var marker : new String[]{"0_str", "0_bool", "0_flags", "0_xml_str", "0_dir"}) {
            expectError("indexed type is not an info value " + marker,
                    "t.bosonoga:3:8: expected a value, got '" + marker + "'",
                    program("info " + marker, "return exit_success"));
            expectError("indexed type is not an initializer " + marker,
                    "t.bosonoga:3:16: expected a value, got '" + marker + "'",
                    program("var value is " + marker, "info value", "return exit_success"));
        }
        expectError("indexed type does not count as a returned value",
                "t.bosonoga:3:3: 'marker_fn' returns 1 value, i32, but this return has 0",
                "def marker_fn ret 0_i32\ndi\n  return 0_str\ndo\n" + program("return exit_success"));
        expectError("indexed type ends a list of values",
                "t.bosonoga:3:14: expected a statement, got '0_str'",
                program("info 1_i32 0_str", "return exit_success"));
        expect("indexed markers remain legal in signatures and bindings", generate(
                "def text_fn ret 0_str di return \"text\"_str do\n"
                        + program("call text_fn with ret 0_str as text di info text do", "return exit_success"))
                .contains("std::string v_text"));
    }

    private static void typedOperandsUseBoundedDisplayText() {
        var prefix = "abcdefghijklmnopqrstuvwxyzabcdef";
        var text = prefix + "_text";
        expectError("sleep operand display is bounded",
                "t.bosonoga:4:12: sleep_ms needs u32, but '" + prefix + "' is str",
                program("var " + text + " is \"text\"_str", "sleep_ms " + text, "return exit_success"));
        expectError("literal operand display includes only the token prefix",
                "t.bosonoga:4:15: set ... to needs bool, but '\"abcdefghijklmnopqrstuvwxyzabcde' is str",
                program("var flag is false_bool", "set flag to \"abcdefghijklmnopqrstuvwxyzabcdef_extra\"_str",
                        "info flag", "return exit_success"));
        expectError("function argument display is bounded",
                "t.bosonoga:8:21: double_fn needs i32, but '" + prefix + "' is bool",
                DOUBLE_FN + program("var " + prefix + "_bool is false_bool",
                        "call double_fn of " + prefix + "_bool with ret 0_i32 as value di info value do",
                        "return exit_success"));
    }

    private static void callDiagnosticsUseBoundedCalleeNames() {
        var prefix = "abcdefghijklmnopqrstuvwxyzabcdef";
        var function = prefix + "_fn";
        expectError("unknown called function display is bounded",
                "t.bosonoga:3:8: unknown function '" + prefix + "'",
                program("call " + function, "return exit_success"));
        expectError("callee argument context uses bounded name",
                "t.bosonoga:4:47: " + prefix + " needs i32, but 'false_bool' is bool",
                "def " + function + " value_i32 ret 0_i32 di return value_i32 do\n"
                        + program("call " + function + " of false_bool with ret 0_i32 as value di info value do",
                                "return exit_success"));
        expectError("missing result binding uses bounded callee name",
                "t.bosonoga:4:3: '" + prefix + "' returns 0_i32, which var, set ... to or call ... with ret takes "
                        + "for its block, each named, as in with ret 0_i32 as value",
                "def " + function + " ret 0_i32 di return 1_i32 do\n"
                        + program("call " + function, "return exit_success"));
        var array = prefix + "_array";
        expect("runtime array diagnostics retain complete names", generate(
                "val " + array + " is array 0_i32 di row 1_i32 do\n"
                        + program("var value is " + array + " at 0_i32 with ret 0_i32", "info value", "return exit_success"))
                .contains(", \"" + array + "\", std::int32_t{0}"));
    }

    private static void selectedProgramsAreAutomaticallyRegistered() {
        var registry = TestRegistry.header(java.util.List.of(Path.of("test/gold/z.bosonoga"),
                Path.of("test/silver/space name.bosonoga"), Path.of("test/silver/class.bosonoga")));
        expect("registration includes encoded names and original test paths",
                registry.contains("int bosonoga_program_636c617373();")
                        && registry.contains("{\"space name\", bosonoga::program::bosonoga_program_7370616365206e616d65}")
                        && registry.contains("\"test/silver\""));
        expect("registration keeps tier and bytewise program order",
                registry.indexOf("{\"silver\"") < registry.indexOf("{\"gold\"")
                        && registry.indexOf("{\"class\"") < registry.indexOf("{\"space name\""));
        expect("empty selection registers no programs",
                TestRegistry.header(java.util.List.of()).contains("#define BOSONOGA_TEST_TIERS {}"));
    }

    private static void unusedSymbolNamesUseTheArchiveDisplayLimit() {
        var prefix = "abcdefghijklmnopqrstuvwxyzabcdef";
        var name = prefix + "_unused";
        expectError("unused variable name is bounded", "t.bosonoga:3:7: '" + prefix + "' is never read",
                program("var " + name + " is 1_i32", "return exit_success"));
        expectError("unused constant name is bounded", "t.bosonoga:1:7: '" + prefix + "' is never used",
                "const " + name + " is 1_i32\n" + program("return exit_success"));
        expectError("unused array name is bounded", "t.bosonoga:1:5: '" + prefix + "' is never read",
                "val " + name + " is array 0_i32 di row 1_i32 do\n" + program("return exit_success"));
        expectError("unused function name is bounded", "t.bosonoga:1:5: '" + prefix + "' is never called",
                "def " + name + "_fn ret_void di nihil do\n" + program("return exit_success"));
        expectError("unused parameter name is bounded", "t.bosonoga:1:13: '" + prefix + "' is never read",
                "def echo_fn " + name + "_i32 ret_void di nihil do\n" + program("return exit_success"));
        expectError("unused global name is bounded", "t.bosonoga:3:7: '" + prefix + "' is never read",
                "bosonoga_global\ndi\n  var " + name + " is 1_i32\ndo\n" + program("return exit_success"));
        expectError("unset unread owner name is bounded", "t.bosonoga:3:7: '" + prefix + "' is never read",
                program("var " + name + " is new str of \"x\"_str", "unset " + name, "return exit_success"));
    }

    private static void hiddenSymbolNamesUseTheArchiveDisplayLimit() {
        var prefix = "abcdefghijklmnopqrstuvwxyzabcdef";
        expectError("private name is bounded", "test/gold/t.bosonoga:4:8: '" + prefix
                        + "' is private to test/gold/long_names.mod.bosonoga",
                () -> generateInGold("mod \"long_wrapper.mod.bosonoga\"_str\n"
                        + program("info " + prefix + "_private", "return exit_success")));
        expectError("pub name outside reader scope is bounded", "test/gold/t.bosonoga:4:8: '" + prefix
                        + "' is pub in test/gold/long_names.mod.bosonoga, so only test/gold/long_wrapper.mod.bosonoga, "
                        + "which reads it with mod, sees it",
                () -> generateInGold("mod \"long_wrapper.mod.bosonoga\"_str\n"
                        + program("info " + prefix + "_public", "return exit_success")));
        expect("long module names still resolve without truncation", generateInGold(
                "mod \"long_wrapper.mod.bosonoga\"_str\n" + program("info wrapped_limit", "return exit_success"))
                .contains("runtime::info(std::cout, std::int32_t{7});"));
    }

    private static void declarationNamesUseTheArchiveDisplayLimit() {
        var prefix = "abcdefghijklmnopqrstuvwxyzabcdef";
        var name = prefix + "_duplicate";
        expectError("duplicate variable name is bounded", "t.bosonoga:4:7: variable '" + prefix + "' already exists",
                program("var " + name + " is 1_i32", "var " + name + " is 2_i32", "return exit_success"));
        expectError("duplicate function name is bounded", "t.bosonoga:2:5: function '" + prefix + "' already exists",
                "def " + name + "_fn ret_void di nihil do\ndef " + name + "_fn ret_void di nihil do\n"
                        + program("return exit_success"));
        expectError("forbidden suffix uses bounded name", "t.bosonoga:3:7: '" + prefix
                        + "' ends with _fn, which only the name of a function does",
                program("var " + name + "_fn is 1_i32", "return exit_success"));
        expect("declaration collision checks use complete names", generate(program(
                "var " + prefix + "_one is 1_i32", "var " + prefix + "_two is 2_i32",
                "info " + prefix + "_one " + prefix + "_two", "return exit_success"))
                .contains("v_" + prefix + "_two"));
    }

    private static void arrayIndicesAreCheckedBeforeExtraIndices() {
        expectError("bad array index type precedes extra index",
                "t.bosonoga:8:18: the index of 'primes' needs i32, but 'false_bool' is bool",
                PRIMES + program("call primes at false_bool 1_i32 with ret 0_u32 as value di info value do",
                        "return exit_success"));
        expectError("unknown array index precedes extra index",
                "t.bosonoga:8:18: unknown variable 'missing'",
                PRIMES + program("call primes at missing 1_i32 with ret 0_u32 as value di info value do",
                        "return exit_success"));
        expectError("valid array index still rejects extra index",
                "t.bosonoga:8:24: 'primes' is an array, which is read at one i32 index, but is given more",
                PRIMES + program("call primes at 0_i32 1_i32 with ret 0_u32 as value di info value do",
                        "return exit_success"));
    }

    private static void arrayAssignmentErrorsDescribeRows() {
        expectError("immutable array assignment describes rows",
                "t.bosonoga:9:7: 'count' is i32, but 'primes' has rows of u32",
                PRIMES + program("var count is 0_i32", "set count to primes at 0_i32 with ret 0_u32",
                        "info count", "return exit_success"));
        expectError("mutable array assignment describes rows",
                "t.bosonoga:5:7: 'count' is i32, but 'rows' has rows of u32",
                program("var rows is array 0_u32 di row 1_u32 do", "var count is 0_i32",
                        "set count to rows at 0_i32 with ret 0_u32", "info count", "return exit_success"));
        expectError("function assignment still describes returns",
                "t.bosonoga:8:7: 'count' is i64, but 'double_fn' returns i32",
                DOUBLE_FN + program("var count is 0_i64", "set count to double_fn of 1_i32 with ret 0_i32",
                        "info count", "return exit_success"));
    }

    private static void loopCounterNamesAreCheckedBeforeBodies() {
        for (var body : new String[]{"di do", "di info missing do", "di info do"}) {
            expectError("counter collision precedes loop body " + body,
                    "t.bosonoga:4:13: variable 'count' already exists",
                    program("var count is 0_i32", "to_2 with count " + body,
                            "info count", "return exit_success"));
        }
        expectError("function suffix counter precedes empty body",
                "t.bosonoga:3:13: 'count_fn' ends with _fn, which only the name of a function does",
                program("to_2 with count_fn di do", "return exit_success"));
        expectError("loop limit precedes counter name",
                "t.bosonoga:4:3: 'to_2147483648' does not fit in 32 bits",
                program("var count is 0_i32", "to_2147483648 with count di do", "info count", "return exit_success"));
    }

    private static void valueNameDiagnosticsUseTheArchiveDisplayLimit() {
        var prefix = "abcdefghijklmnopqrstuvwxyzabcdef";
        var variable = prefix + "_extra";
        var function = prefix + "_extra_fn";
        expectError("unknown variable name is limited to 32 bytes",
                "t.bosonoga:3:8: unknown variable '" + prefix + "'",
                program("info " + variable, "return exit_success"));
        expectError("function classification uses the full name before display truncation",
                "t.bosonoga:3:8: unknown function '" + prefix + "'",
                program("info " + function, "return exit_success"));
        expectError("unknown set target uses bounded name",
                "t.bosonoga:3:7: unknown variable '" + prefix + "'",
                program("set " + variable + " to 1_i32", "return exit_success"));
        expectError("known function value uses bounded name",
                "t.bosonoga:4:8: '" + prefix + "' is a function, which is called with var, set ... to or call",
                "def " + function + " ret 0_i32 di return 1_i32 do\n"
                        + program("info " + function, "return exit_success"));
        expectError("known array value uses bounded name",
                "t.bosonoga:4:8: '" + prefix + "' is an array, which is read with var, set ... to or call",
                "val " + variable + " is array 0_i32 di row 1_i32 do\n"
                        + program("info " + variable, "return exit_success"));
        expect("long identifiers still resolve by their full names", generate(
                "const " + variable + " is 1_i32\nconst " + prefix + "_other is 2_i32\n"
                        + program("info " + variable + " " + prefix + "_other", "return exit_success"))
                .contains("runtime::info(std::cout, std::int32_t{1}, std::int32_t{2});"));
    }

    private static void importedSourcesNeedNoModuleSuffix() {
        var helper = Path.of("test/gold/plain_helper.bosonoga");
        var module = Path.of("test/gold/named_imports.mod.bosonoga");
        var program = Path.of("test/gold/module_discovery.bosonoga");
        expect("discovery scans module files and excludes ordinary imported filenames",
                TestSources.programs(java.util.List.of(helper, module, program)).equals(java.util.List.of(program)));
        expect("unimported ordinary files remain candidates",
                TestSources.programs(java.util.List.of(helper)).equals(java.util.List.of(helper)));
    }

    private static java.util.List<Path> inputList(Path file, String text) {
        try {
            Files.writeString(file, text);
            return Main.inputs(new String[]{"out", "--sources-from", file.toString()});
        } catch (IOException exception) {
            throw new AssertionError(exception);
        }
    }

    private static void sourceListsPreserveWholePaths() {
        try {
            var file = Files.createTempFile("bosonoga-sources-", ".list");
            try {
                var paths = inputList(file, "space name.bosonoga\0tab\tname.bosonoga\0line\nbreak.bosonoga\0");
                expect("source list preserves spaces, tabs and newlines", paths.equals(java.util.List.of(
                        Path.of("space name.bosonoga"), Path.of("tab\tname.bosonoga"), Path.of("line\nbreak.bosonoga"))));
                expect("source list preserves duplicate basenames for validation", inputList(file,
                        "a/same.bosonoga\0b/same.bosonoga\0").size() == 2);
                expectError("source list rejects missing terminator", "source list must end with NUL",
                        () -> inputList(file, "space name.bosonoga").toString());
                expectError("source list rejects empty path", "source list contains an empty path",
                        () -> inputList(file, "first.bosonoga\0\0").toString());
                expect("empty source list is empty", inputList(file, "").isEmpty());
                expect("ordinary CLI filenames remain intact", Main.inputs(new String[]{"out", "space name.bosonoga"})
                        .equals(java.util.List.of(Path.of("space name.bosonoga"))));
            } finally {
                Files.deleteIfExists(file);
            }
        } catch (IOException exception) {
            throw new AssertionError(exception);
        }
    }

    private static void filenamesHaveSafeCppExports() {
        expect("ordinary program exports are unchanged", ProgramName.cpp("hello_world").equals("hello_world"));
        expect("keyword export is encoded", ProgramName.cpp("class").equals("bosonoga_program_636c617373"));
        expect("Unicode and punctuation export is encoded",
                ProgramName.cpp("hello-č").equals("bosonoga_program_68656c6c6f2dc48d"));
        expect("encoding prefix cannot collide with literal filename",
                !ProgramName.cpp("class").equals(ProgramName.cpp("bosonoga_program_636c617373")));
        expect("implementation-reserved names are encoded", !ProgramName.cpp("_X").equals("_X")
                && !ProgramName.cpp("a__b").equals("a__b"));
        expect("generator uses encoded export", Generator.generate(program("return exit_success"),
                "class.bosonoga", "class", SETTINGS).contains("int bosonoga_program_636c617373()"));
        expect("filename control bytes stay inside generated comment", Generator.generate(
                program("return exit_success"), "line\nbreak.bosonoga", "line\nbreak", SETTINGS)
                .startsWith("// Generated from line\\012break.bosonoga by the bosonoga generator. Do not edit.\n"));
    }

    private static void strParseChecksItsNumberBeforeItsBlock() {
        var missingNumber = "t.bosonoga:3:3: str_parse takes its number, whose type it parses, "
                + "as in with ret 0_bool as parsed 1_i32 as number";
        for (var body : new String[]{"di do", "di info missing do", "di info do", "di"}) {
            expectError("str_parse number precedes body error " + body, missingNumber,
                    program("str_parse \"7\"_str with ret 0_bool as parsed " + body,
                            "return exit_success"));
        }
        expectError("str_parse still requires a block opener first",
                "t.bosonoga:3:47: expected another value to take, like 1_i32 as count, or di, got 'nihil'",
                program("str_parse \"7\"_str with ret 0_bool as parsed nihil", "return exit_success"));
        expectError("str_parse with number reports empty block",
                "t.bosonoga:3:63: empty block, write nihil for a block that does nothing",
                program("str_parse \"7\"_str with ret 0_bool as parsed 1_i32 as number di do",
                        "return exit_success"));
        expectError("str_parse with number checks body values",
                "t.bosonoga:3:71: unknown variable 'missing'",
                program("str_parse \"7\"_str with ret 0_bool as parsed 1_i32 as number di info missing do",
                        "return exit_success"));
    }

    private static void voidCallsRejectBareBlocks() {
        var function = "def nothing_fn ret_void\ndi\nnihil\ndo\n";
        expectError("void call rejects bare block",
                "t.bosonoga:7:19: 'nothing_fn' returns no values, so its call has neither with ret nor a block",
                function + program("call nothing_fn di nihil do", "return exit_success"));
        expectError("void call rejection precedes errors inside bare block",
                "t.bosonoga:7:19: 'nothing_fn' returns no values, so its call has neither with ret nor a block",
                function + program("call nothing_fn di info missing do", "return exit_success"));
        expectError("void call rejects empty bare block",
                "t.bosonoga:7:19: 'nothing_fn' returns no values, so its call has neither with ret nor a block",
                function + program("call nothing_fn di do", "return exit_success"));
        expectError("call arguments precede forbidden block",
                "t.bosonoga:7:19: sink_fn needs i32, but 'false_bool' is bool",
                "def sink_fn value_i32 ret_void\ndi\ninfo value_i32\ndo\n"
                        + program("call sink_fn of false_bool di nihil do", "return exit_success"));
        expect("void call without block remains valid", generate(function
                + program("call nothing_fn", "return exit_success")).contains("f_nothing_fn({source, 7, 8});"));
    }

    private static void reservedParametersAreCheckedAfterTheirType() {
        expectError("reserved typed parameter is a name error",
                "t.bosonoga:1:13: expected a variable name, got 'ret_0_i32'",
                "def echo_fn ret_0_i32 ret_void di nihil do\n" + program("return exit_success"));
        expectError("untyped reserved parameter is a type error",
                "t.bosonoga:1:13: expected a parameter whose name ends with its type, like count_i32, got 'ret_0'",
                "def echo_fn ret_0 ret_void di nihil do\n" + program("return exit_success"));
        expectError("earlier duplicate parameter precedes reserved parameter",
                "t.bosonoga:1:23: variable 'value_i32' already exists",
                "def echo_fn value_i32 value_i32 ret_0_i32 ret_void di nihil do\n"
                        + program("return exit_success"));
        expect("ordinary ret-prefixed parameter remains valid", generate(
                "def echo_fn ret_count_i32 ret 0_i32 di return ret_count_i32 do\n"
                        + program("var value is echo_fn of 1_i32 with ret 0_i32", "info value", "return exit_success"))
                .contains("v_ret_count_i32"));
    }

    private static void resultBindingNamesAreCheckedInOrder() {
        expectError("existing binding name precedes later bad type",
                "t.bosonoga:8:34: variable 'count' already exists",
                PAIR_FN + program("var count is 1_i32",
                        "call pair_fn with ret 0_i32 as count 1_i32 as second di info second do",
                        "info count", "return exit_success"));
        expectError("function suffix binding precedes later bad type",
                "t.bosonoga:7:34: 'count_fn' ends with _fn, which only the name of a function does",
                PAIR_FN + program("call pair_fn with ret 0_i32 as count_fn 1_i32 as second di info second do",
                        "return exit_success"));
        expectError("duplicate binding precedes third bad position",
                "t.bosonoga:7:49: variable 'count' already exists",
                PAIR_FN + program("call pair_fn with ret 0_i32 as count 1_f32 as count 2_i32 as third di info third do",
                        "return exit_success"));
        expectError("given binding collision precedes later bad type",
                "t.bosonoga:4:59: variable 'found' already exists",
                program("var found is false_bool",
                        "xml_attribute \"<a/>\"_xml_str \"x\"_str with ret 0_bool as found 1_i32 as text di info text do",
                        "info found", "return exit_success"));
        expectError("str_parse binding collision precedes missing number",
                "t.bosonoga:4:40: variable 'parsed' already exists",
                program("var parsed is false_bool",
                        "str_parse \"7\"_str with ret 0_bool as parsed di info parsed do",
                        "info parsed", "return exit_success"));
        expectError("binding collision precedes malformed next binding",
                "t.bosonoga:8:34: variable 'count' already exists",
                PAIR_FN + program("var count is 1_i32",
                        "call pair_fn with ret 0_i32 as count 1_f32 di nihil do",
                        "info count", "return exit_success"));
        expect("result variables remain local to their block", generate(PAIR_FN
                + program("call pair_fn with ret 0_i32 as count 1_f32 as second di info count second do",
                        "var count is 2_i32", "info count", "return exit_success"))
                .contains("v2_count"));
    }

    private static void topLevelArraysUseVariableNameRules() {
        expectError("array name cannot end in function suffix",
                "t.bosonoga:1:5: 'cells_fn' ends with _fn, which only the name of a function does",
                "val cells_fn is array 0_i32 di row 1_i32 do\n" + program("return exit_success"));
        expectError("array name precedes invalid rows",
                "t.bosonoga:1:5: 'cells_fn' ends with _fn, which only the name of a function does",
                "val cells_fn is array 0_i32 di row false_bool do\n" + program("return exit_success"));
        expectError("array cannot replace constant",
                "t.bosonoga:2:5: a constant with this name already exists",
                "const cells is 1_i32\nval cells is array 0_i32 di row 1_i32 do\n"
                        + program("return exit_success"));
        expectError("array cannot replace array", "t.bosonoga:2:5: 'cells' is already an array",
                "val cells is array 0_i32 di row 1_i32 do\n"
                        + "val cells is array 0_i32 di row 2_i32 do\n" + program("return exit_success"));
        expect("bare suffix remains a valid array name", generate(
                "val _fn is array 0_i32 di row 1_i32 do\n"
                        + program("val count is len_col _fn", "info count", "return exit_success"))
                .contains("a__fn"));
    }

    private static void assignmentsCheckTheirSourceBeforeTheirShape() {
        expectError("source type precedes extra assignment value",
                "t.bosonoga:4:16: set ... to needs i32, but 'false_bool' is bool",
                program("var count is 1_i32", "set count to false_bool 2_i32", "info count", "return exit_success"));
        expectError("unknown source precedes extra assignment value",
                "t.bosonoga:4:16: unknown variable 'missing'",
                program("var count is 1_i32", "set count to missing 2_i32", "info count", "return exit_success"));
        expectError("array lookup precedes length target type",
                "t.bosonoga:4:24: expected the name of an array, got 'missing'",
                program("var count is 1_i64", "set count to len_col missing", "info count", "return exit_success"));
        expectError("length target mismatch names len token", "t.bosonoga:9:16: 'count' is i64, but a len is i32",
                PRIMES + program("var count is 1_i64", "set count to len_col primes", "info count", "return exit_success"));
        expectError("valid first value still rejects extra values",
                "t.bosonoga:4:22: set ... to sets 'count' to a single value",
                program("var count is 1_i32", "set count to 2_i32 3_i32", "info count", "return exit_success"));
    }

    private static void constantsCannotCaptureGlobalVariables() {
        for (var declaration : new String[]{"var", "val"}) {
            expectError("constant cannot capture global " + declaration,
                    "t.bosonoga:5:19: a constant needs a literal or an earlier constant",
                    "bosonoga_global\ndi\n  " + declaration + " source is 7_i32\ndo\n"
                            + "const captured is source\n" + program("info captured", "return exit_success"));
        }
        expectError("text constant cannot capture global variable",
                "t.bosonoga:5:19: a constant needs a literal or an earlier constant",
                "bosonoga_global\ndi\n  var source is \"text\"_str\ndo\n"
                        + "const captured is source\n" + program("info captured", "return exit_success"));
        expectBody("literal aliases remain valid", """
                  runtime::info(std::cout, std::string{"text"});
                  return EXIT_SUCCESS;
                """, "const original is \"text\"_str\nconst alias is original\n"
                + program("info alias", "return exit_success"));
        expectBody("array dimensions remain constant", """
                  runtime::info(std::cout, std::int32_t{2});
                  return EXIT_SUCCESS;
                """, PRIMES + "const count is len_col primes\n" + program("info count", "return exit_success"));
    }

    private static void declarationNamesAreCheckedBeforeInitializers() {
        for (var initializer : new String[]{"missing", "math_to_sqrt 1_i32", "new i32 of 1_i32",
                "array 0_i32 with_len_col 0_i32"}) {
            expectError("duplicate precedes initializer " + initializer,
                    "t.bosonoga:4:7: variable 'value' already exists",
                    program("var value is 1_i32", "var value is " + initializer,
                            "info value", "return exit_success"));
        }
        expectError("constant collision precedes initializer",
                "t.bosonoga:4:7: a constant with this name already exists",
                "const value is 1_i32\n" + program("var value is missing", "return exit_success"));
        expectError("setting collision precedes initializer",
                "t.bosonoga:3:7: expected a variable name, got 'global_laps'",
                program("var global_laps is missing", "return exit_success"));
        expectError("initializer still cannot see its own declaration",
                "t.bosonoga:3:16: unknown variable 'value'",
                program("var value is value", "info value", "return exit_success"));
    }

    private static void reservedResultNamesMatchTheArchive() {
        for (var name : new String[]{"ret_0", "ret_12_i32", "ret_1suffix", "expect"}) {
            expectError("reserved variable " + name, "t.bosonoga:3:7: expected a variable name, got '" + name + "'",
                    program("var " + name + " is 1_i32", "info " + name, "return exit_success"));
            expectError("reserved constant " + name, "t.bosonoga:1:7: expected a variable name, got '" + name + "'",
                    "const " + name + " is 1_i32\n" + program("info " + name, "return exit_success"));
        }
        for (var name : new String[]{"ret_0", "ret_1suffix"}) {
            expectError("legacy result value " + name, "t.bosonoga:3:8: " + name
                            + " is not a value, a call names the values that it takes, as in call ... with ret 0_i32 as count",
                    program("info " + name, "return exit_success"));
        }
        expectError("result binding name is reserved",
                "t.bosonoga:7:34: expected a variable name, got 'ret_0'",
                PAIR_FN + program("call pair_fn with ret 0_i32 as ret_0 di info ret_0 do", "return exit_success"));
        expectError("earlier error precedes reserved name", "t.bosonoga:3:8: unknown variable 'missing'",
                program("info missing", "var ret_0 is 1_i32", "return exit_success"));
        expectBody("ordinary ret names remain valid", """
                  std::int32_t v_ret_count = std::int32_t{1};
                  runtime::info(std::cout, v_ret_count);
                  return EXIT_SUCCESS;
                """, program("var ret_count is 1_i32", "info ret_count", "return exit_success"));
    }

    private static void unknownFunctionValuesAreFunctions() {
        expectError("unknown function in info", "t.bosonoga:3:8: unknown function 'missing_fn'",
                program("info missing_fn", "return exit_success"));
        expectError("unknown function in initializer", "t.bosonoga:3:16: unknown function 'missing_fn'",
                program("var value is missing_fn", "info value", "return exit_success"));
        expectError("unknown function in comparison", "t.bosonoga:3:6: unknown function 'missing_fn'",
                program("if missing_fn eq 1_i32 di nihil do", "return exit_success"));
        expectError("bare suffix is a variable", "t.bosonoga:3:8: unknown variable '_fn'",
                program("info _fn", "return exit_success"));
        expectError("unknown function set target", "t.bosonoga:3:7: unknown function 'missing_fn'",
                program("set missing_fn to 1_i32", "return exit_success"));
        expectError("unknown function unset target", "t.bosonoga:3:9: unknown function 'missing_fn'",
                program("unset missing_fn", "return exit_success"));
        expectError("known function set target",
                "t.bosonoga:7:7: 'pair_fn' is a function, which is called with var, set ... to or call",
                PAIR_FN + program("set pair_fn to 1_i32", "return exit_success"));
        expectError("known function unset target",
                "t.bosonoga:7:9: 'pair_fn' is a function, which is called with var, set ... to or call",
                PAIR_FN + program("unset pair_fn", "return exit_success"));
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
                  std::string v_line = runtime::NewStr().part({source, 4, 26}, std::string{"n "}).part({source, 4, 35}, v_count).take();
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
                  std::string v_line = runtime::NewStr().part({source, 3, 26}, std::string{"a"}).take();
                  runtime::info(std::cout, v_line);
                  runtime::unset(v_line);
                  std::string v2_line = runtime::NewStr().part({source, 6, 26}, std::string{"b"}).take();
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

    private static void reginaNamesValues() {
        expectBody("reginaNamesValues", """
                  runtime::info(std::cout, runtime::Dir{0U, "test", "./test"}, runtime::InputKey{"SAPP_KEYCODE_SPACE"}, runtime::Font{0U, "asset/font/m.ttf", "./asset/font/m.ttf"});
                  return EXIT_SUCCESS;
                """, program("info dir_fixture key_jump font_mono", "return exit_success"));
        expect("reginaNamesValues of a parameter", generate("def held_fn key_input_key ret 0_input_key\ndi\n"
                + "  return key_input_key\ndo\n" + program("var key is held_fn of key_jump with ret 0_input_key",
                "info key", "return exit_success"))
                .contains("runtime::InputKey f_held_fn(const runtime::Where &called_at, "
                        + "runtime::InputKey v_key_input_key) {"));
    }

    private static void reginaValuesCompareOnlyEqually() {
        expectError("reginaValuesCompareOnlyEqually", "t.bosonoga:3:18: dir can only be compared with eq or not_eq",
                program("if dir_fixture gt dir_fixture", "di", "  return exit_success", "do",
                        "return exit_failure"));
        expectError("reginaValuesCompareOnlyEqually of kinds", "t.bosonoga:3:18: cannot compare dir with font",
                program("if dir_fixture eq font_mono", "di", "  return exit_success", "do",
                        "return exit_failure"));
    }

    private static void reginaValueHasNoZero() {
        expectError("reginaValueHasNoZero", "t.bosonoga:3:24: a zeroed row cannot have dir, which has no zero, "
                + "but literal rows can", program("var a is array 0_dir with_len_col 1_i32", "return exit_success"));
    }

    private static void ioReadGivesABlock() {
        expectBody("ioReadGivesABlock", """
                  {
                    const auto result_1 = runtime::io_read({source, 3, 3}, runtime::Dir{0U, "test", "./test"}, std::string{"a.txt"}, true, false);
                    bool v_read = std::get<0>(result_1);
                    const std::string v_text = std::get<1>(result_1);
                    runtime::info(std::cout, v_read, v_text);
                  }
                  return EXIT_SUCCESS;
                """, program("io_read dir_fixture \"a.txt\"_str with ret 0_bool as read 1_str as text",
                "di", "  info read text", "do", "return exit_success"));
        expectError("ioReadGivesABlock without with", "t.bosonoga:4:3: io_read gives 0_bool, 1_xml_str, 2_str, "
                + "which with ret takes for a block, each named, as in with ret 0_bool as done",
                program("io_read dir_fixture \"a.txt\"_str", "return exit_success"));
        expectError("ioReadGivesABlock of a dir", "t.bosonoga:3:11: io_read needs dir, but '\"a\"_str' is str",
                program("io_read \"a\"_str \"a.txt\"_str with ret 0_bool as read", "di", "  info read", "do",
                        "return exit_success"));
    }

    private static void ioWriteWritesText() {
        expect("ioWriteWritesText", generate(program(
                "io_write dir_fixture \"a.xml\"_str \"<a/>\"_xml_str with ret 0_bool as saved 1_bool as taken",
                "di", "  info saved taken", "do", "return exit_success"))
                .contains("runtime::io_write({source, 3, 3}, runtime::Dir{0U, \"test\", \"./test\"}, "
                        + "std::string{\"a.xml\"}, std::string{\"<a/>\"}, true);"));
        expectError("ioWriteWritesText of a number", "t.bosonoga:3:32: io_write writes a str or an xml_str, but "
                + "'1_i32' is i32", program("io_write dir_fixture \"a\"_str 1_i32 with ret 0_bool as saved", "di",
                "  info saved", "do", "return exit_success"));
    }

    private static void ioReasonIsFreedAfterItsBlock() {
        expectError("ioReasonIsFreedAfterItsBlock", "t.bosonoga:5:11: 'reason' is freed after the block "
                + "that io_read gives it, and is not unset", program(
                "io_read dir_fixture \"a\"_str with ret 2_str as reason", "di", "  unset reason", "do",
                "return exit_success"));
        expectError("ioReasonIsFreedAfterItsBlock of io_modified", "t.bosonoga:3:60: 'io_modified' gives 0_bool, "
                + "1_i64, so it has no 2_str", program(
                "io_modified dir_fixture \"a\"_str with ret 0_bool as found 2_str as reason", "di",
                "  info found", "do", "return exit_success"));
    }

    // The modules of test/gold/module.bosonoga, read by a program beside them.
    private static String generateInGold(String source) {
        return Generator.generate(source, "test/gold/t.bosonoga", "t", SETTINGS);
    }

    private static void moduleSeesWhatItsReaderSees() {
        expectError("moduleSeesWhatItsReaderSees", "test/gold/t.bosonoga:4:8: 'unit_count' is pub in "
                + "test/gold/unit.mod.bosonoga, so only test/gold/shape.mod.bosonoga, which reads it with mod, sees it",
                () -> generateInGold("mod \"shape.mod.bosonoga\"_str\n" + program("info unit_count",
                        "return exit_success")));
        expectError("moduleSeesWhatItsReaderSees of a private name", "test/gold/t.bosonoga:4:8: 'hidden' is "
                + "private to test/gold/hidden.mod.bosonoga", () -> generateInGold(
                "mod \"hidden.mod.bosonoga\"_str\n" + program("info hidden",
                        "return exit_success")));
        expectError("moduleSeesWhatItsReaderSees of an unused item", "test/gold/shape.mod.bosonoga:5:11: "
                + "'side_count' is never used", () -> generateInGold("mod \"shape.mod.bosonoga\"_str\n"
                + program("return exit_success")));
    }

    private static void pubIsOnlyInAModule() {
        expectError("pubIsOnlyInAModule", "t.bosonoga:1:1: pub is only in a module, a file read with mod",
                "pub const limit is 1_i32\n" + program("info limit", "return exit_success"));
    }

    private static void modReadsAFileBesideTheProgram() {
        expectError("modReadsAFileBesideTheProgram", "test/gold/t.bosonoga:1:5: mod takes the name of a .bosonoga "
                + "file in the directory of the program, as in mod \"lorem.bosonoga\"_str",
                () -> generateInGold("mod \"../shape.bosonoga\"_str\n" + program("return exit_success")));
        expectError("modReadsAFileBesideTheProgram of a missing file", "test/gold/t.bosonoga:1:15: cannot read "
                + "test/gold/nope.bosonoga: Failed to open file “test/gold/nope.bosonoga”: "
                + "No such file or directory",
                () -> generateInGold("io_raw_import \"nope.bosonoga\"_str\n" + program("return exit_success")));
        expectError("modReadsAFileBesideTheProgram of itself", "test/gold/t.bosonoga:1:5: a program cannot read "
                + "itself with mod", () -> generateInGold("mod \"t.bosonoga\"_str\n" + program("return exit_success")));
        var limit = "io_raw_import \"limit.mod.bosonoga\"_str\n";
        expectError("modReadsAFileBesideTheProgram twice", "test/gold/t.bosonoga:2:15: limit.mod.bosonoga is "
                + "already read, with mod or io_raw_import", () -> generateInGold(limit + limit
                + program("info raw_limit", "return exit_success")));
        expectError("modReadsAFileBesideTheProgram of a value", "t.bosonoga:1:5: expected a file name, as in "
                + "mod \"lorem.bosonoga\"_str, got 'lorem'", "mod lorem\n" + program("return exit_success"));
    }

    private static void importFilenamesContinueAcrossRawEof() {
        expect("raw import filename can follow raw EOF", generateInGold(
                "io_raw_import \"raw_keyword.mod.bosonoga\"_str\n\"limit.mod.bosonoga\"_str\n"
                        + program("info raw_limit", "return exit_success"))
                .contains("runtime::info(std::cout, std::int32_t{10});"));
        expect("module filename can follow raw EOF", generateInGold(
                "io_raw_import \"module_keyword.mod.bosonoga\"_str\n\"raw_scope.mod.bosonoga\"_str\n"
                        + program("info public_limit", "return exit_success"))
                .contains("runtime::info(std::cout, std::int32_t{7});"));
        expectError("continued invalid filename names reader",
                "test/gold/t.bosonoga:2:1: io_raw_import takes the name of a .bosonoga file in the directory of the program, "
                        + "as in io_raw_import \"lorem.bosonoga\"_str",
                () -> generateInGold("io_raw_import \"raw_keyword.mod.bosonoga\"_str\n\"../bad.bosonoga\"_str\n"
                        + program("return exit_success")));
        expectError("module EOF does not supply an import filename",
                "test/gold/raw_keyword.mod.bosonoga:2:1: expected a file name, as in io_raw_import \"lorem.bosonoga\"_str, got end of file",
                () -> generateInGold("mod \"raw_keyword.mod.bosonoga\"_str\n\"limit.mod.bosonoga\"_str\n"
                        + program("return exit_success")));
    }

    private static void rawImportsContinueInTheirReader() {
        expect("raw initializer continues in reader", generateInGold(
                "io_raw_import \"constant_prefix.mod.bosonoga\"_str\n7_i32\n"
                        + program("info imported_limit", "return exit_success"))
                .contains("runtime::info(std::cout, std::int32_t{7});"));
        expect("nested raw EOFs are transparent", generateInGold(
                "io_raw_import \"raw_chain.mod.bosonoga\"_str\n7_i32\n"
                        + program("info imported_limit", "return exit_success"))
                .contains("runtime::info(std::cout, std::int32_t{7});"));
        expectError("raw continuation error names reader", "test/gold/t.bosonoga:2:1: unknown variable 'missing'",
                () -> generateInGold("io_raw_import \"constant_prefix.mod.bosonoga\"_str\nmissing\n"
                        + program("return exit_success")));
        expectError("module EOF still ends an incomplete function",
                "test/gold/function_prefix.mod.bosonoga:4:3: 'imported_identity_fn' returns 1 value, i32, but this return has 0",
                () -> generateInGold("mod \"function_prefix.mod.bosonoga\"_str\n1_i32\ndo\n"
                        + program("return exit_success")));
        expect("raw import inside module retains module scope", generateInGold(
                "mod \"raw_scope.mod.bosonoga\"_str\n" + program("info public_limit", "return exit_success"))
                .contains("runtime::info(std::cout, std::int32_t{7});"));
        expectError("raw private constant stays inside module",
                "test/gold/t.bosonoga:4:8: 'imported_limit' is private to test/gold/raw_scope.mod.bosonoga",
                () -> generateInGold("mod \"raw_scope.mod.bosonoga\"_str\n"
                        + program("info imported_limit public_limit", "return exit_success")));
    }

    private static void moduleNamesItsFileInErrors() {
        expect("moduleNamesItsFileInErrors", generateInGold("mod \"shape.mod.bosonoga\"_str\n" + program(
                "var area is square_fn of side_count with ret 0_i32",
                "call corners at 0_i32 with ret 0_str as corner",
                "di",
                "  info area corner",
                "do",
                "return exit_success"))
                .contains("constexpr const char *source_1 = \"test/gold/shape.mod.bosonoga\";\n"));
    }

    private static void blockSaysWhatItExpects() {
        expectError("blockSaysWhatItExpects", "t.bosonoga:2:1: empty block, write nihil for a block that does "
                + "nothing", "bosonoga_main\ndi\ndo\n");
        expectError("blockSaysWhatItExpects of a statement", "t.bosonoga:3:23: expected a statement, got '1_i32'",
                program("return exit_success 1_i32"));
        expectError("blockSaysWhatItExpects at the end", "t.bosonoga:4:1: expected do, got end of file",
                "bosonoga_main\ndi\n  return exit_success\n");
        expectError("blockSaysWhatItExpects of di", "t.bosonoga:2:1: expected di, got end of file",
                "bosonoga_main\n");
        expectError("blockSaysWhatItExpects of a name", "t.bosonoga:3:7: expected a variable name, got 'while'",
                program("var while is 1_i32", "return exit_success"));
        expectError("blockSaysWhatItExpects of a long token", "t.bosonoga:3:3: expected a statement, got "
                + "'\"ččččččččččččččč\uFFFD'", program("\"čččččččččččččččč\"_str", "return exit_success"));
    }

    private static void topLevelSaysWhatItExpects() {
        expectError("topLevelSaysWhatItExpects", "t.bosonoga:1:1: expected const, def, val, bosonoga_global, "
                + "bosonoga_main, gfx_init, gfx_frame, gfx_cleanup, gfx_key_down or gfx_main, got 'info'",
                "info 1_i32\n" + program("return exit_success"));
        expectError("topLevelSaysWhatItExpects of var", "t.bosonoga:1:1: a var is a variable of a block, and a "
                + "top-level array is a val", "var x is 1_i32\n" + program("return exit_success"));
        expectError("topLevelSaysWhatItExpects after main", "t.bosonoga:5:1: expected const, def, val or end of "
                + "file, got 'info'", program("return exit_success") + "info 1_i32\n");
    }

    private static void stringIsCheckedAsItIsRead() {
        expectError("stringIsCheckedAsItIsRead", "t.bosonoga:3:14: escape sequences are not supported",
                program("info \"it's \\\"q\\\"\"_str", "return exit_success"));
        expectError("stringIsCheckedAsItIsRead of an end", "t.bosonoga:3:8: unterminated string",
                program("info \"open", "return exit_success"));
        expectError("stringIsCheckedAsItIsRead of a suffix", "t.bosonoga:3:8: a string must have the suffix _str "
                + "or _xml_str, as in \"text\"_str", program("info \"a=\"1.0\"_str", "return exit_success"));
    }

    private static void numberIsCheckedAsItIsRead() {
        expectError("numberIsCheckedAsItIsRead", "t.bosonoga:3:8: the decimal separator is the letter d, as in 1d5_f32",
                program("info 1.5_f32", "return exit_success"));
        expectError("numberIsCheckedAsItIsRead of a suffix", "t.bosonoga:3:8: number must have the suffix _i32, "
                + "_i64, _u32, _u64, _f32 or _f64", program("info 1x", "return exit_success"));
        expectError("numberIsCheckedAsItIsRead of d", "t.bosonoga:3:8: expected a digit after the decimal separator d",
                program("info 1d_f32", "return exit_success"));
        expectError("numberIsCheckedAsItIsRead of an index", "t.bosonoga:3:8: an index is digits and a type, like 1_str",
                program("info -1_str", "return exit_success"));
        expectError("numberIsCheckedAsItIsRead of a character", "t.bosonoga:3:8: unexpected character",
                program("info $", "return exit_success"));
    }

    // As the C bosonoga reads a program once, what is before a syntax error fails first.
    private static void errorsComeInTheOrderOfTheProgram() {
        expectError("errorsComeInTheOrderOfTheProgram", "t.bosonoga:3:8: unknown variable 'nope'",
                program("info nope", "info 1_i32 is", "return exit_success"));
        expectError("errorsComeInTheOrderOfTheProgram in a statement", "t.bosonoga:3:7: unknown variable 'nope'",
                program("set nope is", "return exit_success"));
        expectError("errorsComeInTheOrderOfTheProgram at the end of a block", "t.bosonoga:3:7: 'unread' is never "
                + "read", "bosonoga_main\ndi\n  var unread is 1_i32\n");
        expectError("errorsComeInTheOrderOfTheProgram of a number", "t.bosonoga:3:8: unknown variable 'nope'",
                program("info nope 99999999999_i32", "return exit_success"));
        expectError("errorsComeInTheOrderOfTheProgram of a comparison", "t.bosonoga:3:12: expected a comparison, "
                + "got '-7_i32'", program("if 1_i32 -7_i32", "di", "  nihil", "do", "return exit_success"));
    }

    private static void syntaxSaysWhatItExpects() {
        expectError("syntaxSaysWhatItExpects", "t.bosonoga:3:12: expected a value, got 'do'",
                program("var x is do", "return exit_success"));
        expectError("syntaxSaysWhatItExpects of var", "t.bosonoga:3:9: a var declares one variable, and the values "
                + "of a call are taken with call ... with ret, one per line", program("var x y is 1_i32",
                "return exit_success"));
        expectError("syntaxSaysWhatItExpects of set", "t.bosonoga:4:9: expected to, to_add, to_subtract, "
                + "to_multiply, to_divide, to_mod or a conversion, got '1_i32'", program("var x is 1_i32",
                "set x 1_i32", "info x", "return exit_success"));
        expectError("syntaxSaysWhatItExpects of return", "t.bosonoga:3:10: expected exit_success or exit_failure, "
                + "got 'is'", program("return is"));
        expectError("syntaxSaysWhatItExpects of a type", "t.bosonoga:2:1: expected a type with its index, like "
                + "0_i32, of i32, i64, u32, u64, f32, f64, flags, color, str or bool, got 'di'",
                "def one_fn ret\ndi\n  return 1_i32\ndo\n" + program("return exit_success"));
        expectError("syntaxSaysWhatItExpects of else", "t.bosonoga:6:11: expected di, got 'nihil'",
                program("if true_bool", "di", "  nihil", "do else nihil", "return exit_success"));
        expectError("syntaxSaysWhatItExpects of shift_or", "t.bosonoga:4:1: expected a value, got 'do'",
                "bosonoga_main\ndi\n  var flags is shift_0 shift_or\ndo\n");
    }

    private static void callSaysWhatItTakes() {
        var square = "def square_fn n_i32 ret 0_i32\ndi\n  return n_i32\ndo\n";
        expectError("callSaysWhatItTakes", "t.bosonoga:7:22: 'square_fn' takes 1 value, i32, which follow of, "
                + "as in square_fn of ...", square + program("var a is square_fn 3_i32 with ret 0_i32",
                "return exit_success"));
        expectError("callSaysWhatItTakes of with", "t.bosonoga:7:36: expected ret, got '0_i32'",
                square + program("var a is square_fn of 3_i32 with 0_i32", "info a", "return exit_success"));
        expectError("callSaysWhatItTakes of ret", "t.bosonoga:8:3: expected with ret 0_i32, the value of "
                + "'square_fn' that is taken, got 'info'", square + program("var a is square_fn of 3_i32",
                "info a", "return exit_success"));
        expectError("callSaysWhatItTakes of as", "t.bosonoga:4:1: expected as and a name after 0_bool, as in "
                + "0_bool as value, got 'do'", program("str_parse \"1\"_str with ret 0_bool"));
    }

    // As the C bosonoga counts the column in bytes.
    private static void columnCountsBytes() {
        expectError("columnCountsBytes", "t.bosonoga:3:19: unknown variable 'nope'",
                program("info \"čć\"_str nope", "return exit_success"));
    }

    private static void pubVariableIsOfAModule() {
        expectError("pubVariableIsOfAModule", "t.bosonoga:3:3: pub is only in a module, a file read with mod",
                "bosonoga_global\ndi\n  pub var shared is 1_i32\ndo\n" + program("info shared",
                        "return exit_success"));
    }

    private static void fileNameIsBuiltAsGLibDoes() {
        expectEquals("fileNameIsBuiltAsGLibDoes", "./save", Settings.buildFilename(".", "save"));
        expectEquals("fileNameIsBuiltAsGLibDoes of a trailing /", "./save/", Settings.buildFilename(".", "save/"));
        expectEquals("fileNameIsBuiltAsGLibDoes of many /", "a/b//c", Settings.buildFilename("a//", "//b//c"));
        expectEquals("fileNameIsBuiltAsGLibDoes of the root", "/", Settings.buildFilename("/", ""));
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
