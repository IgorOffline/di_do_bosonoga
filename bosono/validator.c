#include "validator.h"

bool valid_utf8(char const* text, size_t length) {
    size_t position = 0;
    while (position < length) {
        unsigned char const first = (unsigned char)text[position++];
        if (first <= 0x7f) {
            continue;
        }
        size_t continuation_count;
        unsigned int codepoint;
        unsigned int minimum;
        if (first >= 0xc2 && first <= 0xdf) {
            continuation_count = 1;
            codepoint = first & 0x1fU;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            continuation_count = 2;
            codepoint = first & 0x0fU;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            continuation_count = 3;
            codepoint = first & 0x07U;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (continuation_count > length - position) {
            return false;
        }
        for (size_t index = 0; index < continuation_count; index++) {
            unsigned char const next = (unsigned char)text[position++];
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6) | (next & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
            return false;
        }
    }
    return true;
}
