#!/bin/sh
set -eu

cd "$(dirname "$0")"

export PKG_CONFIG_PATH="$PWD/bosonoga:/home/igor/dev/glib-2.90.1/build/meson-uninstalled"

clang-format -i --style=Google bosonoga/include/bosonoga/*.h bosonoga/src/*.h bosonoga/src/*.c bosonoga/src/parser/*.h bosonoga/src/parser/*.c bosonoga/test/main.c
bosonoga/build.sh
clang -std=c11 -O0 -g -fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wcast-qual -Wcast-align -Wwrite-strings -Wstrict-prototypes -Wmissing-prototypes -Wmissing-variable-declarations -Wredundant-decls -Wnull-dereference -Wdouble-promotion -Wfloat-equal -Wformat=2 -Wundef -Wswitch-enum -Wunused -Wunreachable-code -Wvla $(pkg-config --cflags bosonoga | sed 's/-I/-isystem /g') bosonoga/test/main.c $(pkg-config --static --libs bosonoga) -Wl,-rpath,/home/igor/dev/glib-2.90.1/build/glib -o bosonoga/build/test
suite_output=$(bosonoga/build/test)
for level in 1 2 3 4; do
  for test_file in "bosonoga/impl/level_$level/"*.bosonoga; do
    case "$test_file" in *.xp.bosonoga) continue ;; esac
    if test_output=$(bosonoga/build/test "$test_file" 2>&1); then
      :
    else
      printf '%s\n' "$test_output" >&2
      exit 1
    fi
  done
done

printf '%s\n' "$suite_output" | awk '{ lines[NR] = $0 } END { printf "%s\n", lines[NR]; for (i = 1; i < NR; i++) print lines[i] }'
