#!/bin/sh
# Needs clang++ on PATH; everything for Java is under ../java:
#   jdk*                  the one and only JDK, which must be a JDK 25
#   antlr4-4.13.2         ANTLR 4 source, for its runtime and its tool
#   antlr-complete-3.5.3  ANTLR 3 complete jar, unpacked, which writes the ANTLR 4
#                         tool's own parsers and brings ST4 and the ANTLR 3 runtime
#   icu4j*                the one and only icu4j jar, unpacked, which writes the
#                         ANTLR 4 tool's table of Unicode properties for \p{...}
# ANTLR is built once into build/antlr; remove that directory to rebuild it.
set -eu
cd "$(dirname "$0")"

: "${JAVA_DIR:=../java}"
: "${ANTLR4_SRC:=$JAVA_DIR/antlr4-4.13.2}"
: "${ANTLR3:=$JAVA_DIR/antlr-complete-3.5.3}"
: "${CXX:=clang++}"
: "${CXXFLAGS:=-std=c++17 -O0 -g -fsanitize=undefined -fno-sanitize-recover=all -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow}"

fail() {
  echo "build.sh: $*" >&2
  exit 1
}

# Prints the one entry among the matches of a glob, so that a second version,
# or a stray archive beside the unpacked one, fails the build.
only_one() {
  what=$1
  shift
  count=0
  found=
  for entry in "$@"; do
    [ -e "$entry" ] || continue
    count=$((count + 1))
    found=$entry
  done
  [ "$count" -eq 1 ] || fail "$JAVA_DIR must hold exactly one $what, but it holds $count"
  echo "$found"
}

JAVA_HOME=$(only_one JDK "$JAVA_DIR"/jdk*)
[ -x "$JAVA_HOME/bin/java" ] && [ -x "$JAVA_HOME/bin/javac" ] ||
  fail "$JAVA_HOME has no bin/java and bin/javac"
case "$(grep '^JAVA_VERSION=' "$JAVA_HOME/release" 2>/dev/null || true)" in
  'JAVA_VERSION="25"' | 'JAVA_VERSION="25.'*) ;;
  *) fail "$JAVA_HOME is not a JDK 25" ;;
esac
export JAVA_HOME
JAVA=$JAVA_HOME/bin/java
JAVAC=$JAVA_HOME/bin/javac

ICU4J=$(only_one icu4j "$JAVA_DIR"/icu4j*)
[ -f "$ICU4J/com/ibm/icu/lang/UCharacter.class" ] || fail "no unpacked icu4j jar in $ICU4J"

ANTLR4_RUNTIME_SRC=$ANTLR4_SRC/runtime/Java/src
ANTLR4_TOOL_SRC=$ANTLR4_SRC/tool/src
ANTLR4_TOOL_RESOURCES=$ANTLR4_SRC/tool/resources
TREELAYOUT_JAR=$ANTLR4_SRC/runtime/Java/lib/org.abego.treelayout.core.jar
[ -d "$ANTLR4_RUNTIME_SRC/org/antlr/v4/runtime" ] || fail "no ANTLR 4 runtime source in $ANTLR4_RUNTIME_SRC"
[ -d "$ANTLR4_TOOL_SRC/org/antlr/v4/parse" ] || fail "no ANTLR 4 tool source in $ANTLR4_TOOL_SRC"
[ -f "$TREELAYOUT_JAR" ] || fail "no $TREELAYOUT_JAR"
[ -f "$ANTLR3/org/antlr/Tool.class" ] || fail "no unpacked ANTLR 3 complete jar in $ANTLR3"

ANTLR=build/antlr
RUNTIME=$ANTLR/runtime
TOOL=$ANTLR/tool
TOOL_CLASSPATH=$TOOL:$ANTLR4_TOOL_RESOURCES:$RUNTIME:$ANTLR3:$TREELAYOUT_JAR

if [ ! -f "$ANTLR/built" ]; then
  rm -rf "$ANTLR"
  mkdir -p "$RUNTIME" "$TOOL" "$ANTLR/tool_generated" "$ANTLR/unicode_writer"
  PARSE_OUT=$ANTLR/tool_generated/org/antlr/v4/parse
  CODEGEN_OUT=$ANTLR/tool_generated/org/antlr/v4/codegen
  UNICODE_CONTROLLER=$ANTLR4_TOOL_SRC/org/antlr/v4/unicode/UnicodeDataTemplateController.java
  # ANTLR 3 rejects a -lib directory that does not exist yet.
  mkdir -p "$PARSE_OUT" "$CODEGEN_OUT"

  # The runtime and tool sources predate Java 25, so their warnings are muted.
  # shellcheck disable=SC2046
  "$JAVAC" --release 25 -encoding UTF-8 -nowarn -Xlint:-removal -d "$RUNTIME" \
    $(find "$ANTLR4_RUNTIME_SRC" -name '*.java')

  # One grammar per run, in vocabulary order: ANTLRParser reads the tokens of
  # ANTLRLexer, and the tree grammars read the tokens of ANTLRParser.
  for grammar in ANTLRLexer ANTLRParser ActionSplitter ATNBuilder \
    BlockSetTransformer GrammarTreeVisitor LeftRecursiveRuleWalker; do
    "$JAVA" -cp "$ANTLR3" org.antlr.Tool -fo "$PARSE_OUT" -lib "$PARSE_OUT" \
      "$ANTLR4_TOOL_SRC/org/antlr/v4/parse/$grammar.g"
  done
  "$JAVA" -cp "$ANTLR3" org.antlr.Tool -fo "$CODEGEN_OUT" -lib "$PARSE_OUT" \
    "$ANTLR4_TOOL_SRC/org/antlr/v4/codegen/SourceGenTriggers.g"

  # Only the writer needs icu4j, so it stays off the tool's classpath.
  "$JAVAC" --release 25 -encoding UTF-8 -nowarn -Xlint:-removal \
    -cp "$RUNTIME:$ANTLR3:$ICU4J" -d "$ANTLR/unicode_writer" \
    "$UNICODE_CONTROLLER" antlr_tool/bosonoga/antlr/UnicodeDataWriter.java
  "$JAVA" -cp "$ANTLR/unicode_writer:$RUNTIME:$ANTLR3:$ICU4J" bosonoga.antlr.UnicodeDataWriter \
    "$ANTLR4_TOOL_RESOURCES/org/antlr/v4/tool/templates" \
    "$ANTLR/tool_generated/org/antlr/v4/unicode/UnicodeData.java"

  # shellcheck disable=SC2046
  "$JAVAC" --release 25 -encoding UTF-8 -nowarn -Xlint:-removal \
    -cp "$RUNTIME:$ANTLR3:$TREELAYOUT_JAR" -d "$TOOL" \
    $(find "$ANTLR4_TOOL_SRC" "$ANTLR/tool_generated" -name '*.java' \
      ! -name UnicodeDataTemplateController.java)

  touch "$ANTLR/built"
fi

rm -rf build/bosonoga build/main
mkdir -p build/bosonoga/generated build/bosonoga/classes build/bosonoga/test_classes \
  build/bosonoga/cc

"$JAVA" -cp "$TOOL_CLASSPATH" org.antlr.v4.Tool -Dlanguage=Java -visitor -no-listener \
  -package bosonoga.generator -Xexact-output-dir -o build/bosonoga/generated \
  grammar/Bosonoga.g4

# shellcheck disable=SC2046
"$JAVAC" --release 25 -encoding UTF-8 -cp "$RUNTIME" -d build/bosonoga/classes \
  $(find src build/bosonoga/generated -name '*.java')
# shellcheck disable=SC2046
"$JAVAC" --release 25 -encoding UTF-8 -cp "build/bosonoga/classes:$RUNTIME" \
  -d build/bosonoga/test_classes $(find test/java -name '*.java')
"$JAVA" -cp "build/bosonoga/test_classes:build/bosonoga/classes:$RUNTIME" \
  bosonoga.generator.GeneratorTest

# Each test/<tier>/NAME.bosonoga becomes build/bosonoga/cc/NAME.cc, which main.cc
# runs as bosonoga::program::NAME, and checks against NAME.xp.bosonoga if there is one.
# shellcheck disable=SC2046
"$JAVA" -cp "build/bosonoga/classes:$RUNTIME" bosonoga.generator.Main build/bosonoga/cc \
  $(find test -name '*.bosonoga' ! -name '*.xp.bosonoga' | sort)
# bosonoga lets a variable be compared with, or set to, itself, which clang
# would otherwise reject.
# shellcheck disable=SC2086
$CXX $CXXFLAGS -Wno-tautological-compare -Wno-self-assign -I runtime \
  main.cc build/bosonoga/cc/*.cc -o build/main
./build/main
