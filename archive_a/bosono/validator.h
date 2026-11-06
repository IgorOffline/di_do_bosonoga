#ifndef BOSONO_VALIDATOR_H
#define BOSONO_VALIDATOR_H

#include <stdbool.h>
#include <stddef.h>

bool valid_utf8(char const* text, size_t length);

#endif
