#!/bin/sh
# Builds build/libbosonoga.a, the static library of the bosonoga language, and
# build/libbosonoga_gfx.a, which runs gfx programs in a window with sokol. A
# project links with them through bosonoga.pc and bosonoga_gfx.pc, which are
# next to this script:
#
#   PKG_CONFIG_PATH=path/to/bosonoga pkg-config --static --cflags --libs bosonoga
#   PKG_CONFIG_PATH=path/to/bosonoga pkg-config --static --cflags --libs \
#     bosonoga_gfx
#
# GLib is found with pkg-config too, so PKG_CONFIG_PATH must also have the
# directory of glib-2.0.pc. CC and CFLAGS can be set to build differently.
set -eu
cd "$(dirname "$0")"

: "${CC:=clang}"
: "${CFLAGS:=-std=c11 -O0 -g -fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wcast-qual -Wcast-align -Wwrite-strings -Wstrict-prototypes -Wmissing-prototypes -Wmissing-variable-declarations -Wredundant-decls -Wnull-dereference -Wdouble-promotion -Wfloat-equal -Wformat=2 -Wundef -Wswitch-enum -Wunused -Wunreachable-code -Wvla}"
GLIB_CFLAGS=$(pkg-config --cflags glib-2.0 | sed 's/-I/-isystem /g')

rm -rf build
mkdir -p build/obj
for source in src/*.c src/parser/*.c; do
  # The window backend is independent of the console library.
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
