#include "parser.h"

#include <stdio.h>
#include <stdlib.h>

int parse(Tokens* tokens) {
    for (size_t index = 0; index < tokens->count; index++) {
        Token* const token = &tokens->items[index];
        printf("[%.*s]\n", (int)token->length, token->text);
        free(token->text);
        *token = (Token){0};
    }
    free(tokens->items);
    *tokens = (Tokens){0};
    return 10;
}
