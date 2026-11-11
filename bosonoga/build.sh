#!/bin/sh
# A project links with the libraries through bosonoga.pc and bosonoga_gfx.pc,
# with the directory of glib-2.0.pc also in PKG_CONFIG_PATH.
set -eu
cd "$(dirname "$0")"

: "${CC:=clang}"
: "${CFLAGS:=-std=c11 -O0 -g -fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wcast-qual -Wcast-align -Wwrite-strings -Wstrict-prototypes -Wmissing-prototypes -Wmissing-variable-declarations -Wredundant-decls -Wnull-dereference -Wdouble-promotion -Wfloat-equal -Wformat=2 -Wundef -Wswitch-enum -Wunused -Wunreachable-code -Wvla}"
GLIB_CFLAGS=$(pkg-config --cflags glib-2.0 | sed 's/-I/-isystem /g')

rm -rf build
mkdir -p build/obj
for source in src/*.c src/parser/*.c; do
  case "$source" in src/gfx.c | src/gfx_vendor.c) continue ;; esac
  object="build/obj/$(echo "${source#src/}" | tr / _).o"
  # shellcheck disable=SC2086
  $CC $CFLAGS $GLIB_CFLAGS -Iinclude -Isrc \
    -c "$source" -o "$object"
done
ar rcs build/libbosonoga.a build/obj/*.o

mkdir -p build/gfx_obj
for source in src/gfx.c src/gfx_vendor.c; do
  object="build/gfx_obj/$(basename "$source" .c).o"
  # shellcheck disable=SC2086
  $CC $CFLAGS $GLIB_CFLAGS -Iinclude -Isrc -isystem sokol -isystem fontstash \
    -pthread -c "$source" -o "$object"
done
ar rcs build/libbosonoga_gfx.a build/gfx_obj/*.o
