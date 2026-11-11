#include "key.h"

static const struct {
  const gchar *name;
  guint32 code;
} bosonoga_keys[] = {
    {"A", 65},          {"B", 66},       {"C", 67},      {"D", 68},
    {"E", 69},          {"F", 70},       {"G", 71},      {"H", 72},
    {"I", 73},          {"J", 74},       {"K", 75},      {"L", 76},
    {"M", 77},          {"N", 78},       {"O", 79},      {"P", 80},
    {"Q", 81},          {"R", 82},       {"S", 83},      {"T", 84},
    {"U", 85},          {"V", 86},       {"W", 87},      {"X", 88},
    {"Y", 89},          {"Z", 90},       {"Key0", 48},   {"Key1", 49},
    {"Key2", 50},       {"Key3", 51},    {"Key4", 52},   {"Key5", 53},
    {"Key6", 54},       {"Key7", 55},    {"Key8", 56},   {"Key9", 57},
    {"Space", 32},      {"Escape", 256}, {"Enter", 257}, {"Tab", 258},
    {"Backspace", 259}, {"Right", 262},  {"Left", 263},  {"Down", 264},
    {"Up", 265},
};

gboolean bosonoga_key_named(const gchar *name, guint32 *code) {
  for (gsize i = 0; i < G_N_ELEMENTS(bosonoga_keys); ++i) {
    if (g_str_equal(bosonoga_keys[i].name, name)) {
      *code = bosonoga_keys[i].code;
      return TRUE;
    }
  }
  return FALSE;
}

const gchar *bosonoga_key_name(guint32 code) {
  for (gsize i = 0; i < G_N_ELEMENTS(bosonoga_keys); ++i) {
    if (bosonoga_keys[i].code == code) {
      return bosonoga_keys[i].name;
    }
  }
  return "?";
}
