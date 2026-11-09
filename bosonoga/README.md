# bosonoga

The bosonoga programming language as a C11 static library on GLib: it parses
`.bosonoga` programs, runs them as tests, and runs gfx programs in a window
with sokol. The grammar is documented in `include/bosonoga/parser.h`.

## Using it

Build `build/libbosonoga.a`, then compile and link through `bosonoga.pc`. GLib
is found with pkg-config too, so `PKG_CONFIG_PATH` also needs the directory of
`glib-2.0.pc`:

```sh
export PKG_CONFIG_PATH=path/to/bosonoga:path/to/glib/pkgconfig
path/to/bosonoga/build.sh
clang -std=c11 $(pkg-config --cflags bosonoga) main.c \
  $(pkg-config --static --libs bosonoga) -o main
```

The library is compiled with `-fsanitize=undefined` by default, so a program
that links with it does the same, or rebuilds it with other `CFLAGS`.

Only `include/bosonoga/` is the public interface:

- `bosonoga.h`: values, errors and exit statuses.
- `settings.h`: reads the settings file of a project, `regina.ini` by
  convention, whose colors and globals a program can name.
- `parser.h`: the grammar, and `bosonoga_parse`.
- `interpreter.h`: runs a program without a window.
- `gfx.h`: runs a gfx program in a window.
- `test_runner.h`: runs one test or a directory of tests, in the byte order of
  their names, each of which passes when it returns `exit_success` and prints
  what its `.xp.bosonoga` file expects, one `expect` line per printed line.

## Developing it

The `debug.txt` at the root of the repository formats the sources of the
library and of daria, builds the library, runs the tests of the language in
`test/`, with the settings in `test/regina.ini`, and then runs daria. The tests are in
three tiers, each a directory, which are run in this order:

- `test/silver/`: beginner, a `bosonoga_main` with values and arithmetic.
- `test/gold/`: advanced, such as flags and a first gfx program.
- `test/platinum/`: expert, such as every section of a gfx program and the
  variables that they share.

`build/test path/to/test.bosonoga` runs a single test, and
`G_MESSAGES_DEBUG=all` shows why a test fails.
