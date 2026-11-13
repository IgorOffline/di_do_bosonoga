#include <string.h>

#include "parser/internal.h"

static const BosonogaScope *bosonoga_scope(const BosonogaParser *parser,
                                           guint scope) {
  return &g_array_index(parser->scopes, BosonogaScope, scope);
}

// Where a declaration is seen, with the scopes inside it.
static guint bosonoga_home(const BosonogaParser *parser,
                           const BosonogaDeclaration *declaration) {
  return declaration->is_pub
             ? bosonoga_scope(parser, declaration->scope)->parent
             : declaration->scope;
}

gboolean bosonoga_is_visible(const BosonogaParser *parser,
                             const BosonogaDeclaration *declaration) {
  const guint home = bosonoga_home(parser, declaration);
  guint scope = parser->scope;
  while (scope != home) {
    if (scope == 0) {
      return FALSE;
    }
    scope = bosonoga_scope(parser, scope)->parent;
  }
  return TRUE;
}

void bosonoga_declare_name(BosonogaParser *parser,
                           BosonogaDeclaration *declaration,
                           const BosonogaToken *name) {
  memset(declaration, 0, sizeof(*declaration));
  declaration->token = *name;
  declaration->scope = parser->scope;
}

void bosonoga_append_function(BosonogaParser *parser,
                              const BosonogaFunction *function,
                              const BosonogaToken *name) {
  BosonogaDeclaration declaration;
  bosonoga_declare_name(parser, &declaration, name);
  g_array_append_val(parser->functions, *function);
  g_array_append_val(parser->function_declarations, declaration);
}

static const BosonogaDeclaration *bosonoga_find_hidden(
    const BosonogaParser *parser, const BosonogaToken *token) {
  const BosonogaConstant *constant = bosonoga_find_constant_any(parser, token);
  if (constant != NULL) {
    return &constant->declaration;
  }
  const BosonogaVariable *variable = bosonoga_find_variable_any(parser, token);
  if (variable != NULL) {
    return &variable->declaration;
  }
  const gint function = bosonoga_find_function_any(parser, token);
  if (function >= 0) {
    return &g_array_index(parser->function_declarations, BosonogaDeclaration,
                          (guint)function);
  }
  return NULL;
}

gboolean bosonoga_check_not_hidden(const BosonogaParser *parser,
                                   const BosonogaToken *token, GError **error) {
  if (token->kind != BOSONOGA_TOKEN_WORD) {
    return TRUE;
  }
  const BosonogaDeclaration *declaration = bosonoga_find_hidden(parser, token);
  if (declaration == NULL || bosonoga_is_visible(parser, declaration)) {
    return TRUE;
  }
  g_autofree gchar *shown = bosonoga_token_shown(token);
  const BosonogaScope *scope = bosonoga_scope(parser, declaration->scope);
  if (!declaration->is_pub) {
    return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_NAME,
                            "'%s' is private to %s", shown, scope->path);
  }
  return bosonoga_fail_at(parser, token, error, BOSONOGA_ERROR_NAME,
                          "'%s' is pub in %s, so only %s, which reads it with "
                          "mod, sees it",
                          shown, scope->path,
                          bosonoga_scope(parser, scope->parent)->path);
}

gboolean bosonoga_fail_unused(const BosonogaParser *parser,
                              const BosonogaDeclaration *declaration,
                              const gchar *what, GError **error) {
  g_autofree gchar *shown = bosonoga_token_shown(&declaration->token);
  return bosonoga_fail_at(parser, &declaration->token, error,
                          BOSONOGA_ERROR_NAME, "'%s' is never %s", shown, what);
}

gboolean bosonoga_check_variables_used(const BosonogaParser *parser,
                                       guint first, GError **error) {
  for (guint i = first; i < parser->variables->len; ++i) {
    const BosonogaVariable *variable =
        &g_array_index(parser->variables, BosonogaVariable, i);
    if (!variable->declaration.is_used) {
      return bosonoga_fail_unused(parser, &variable->declaration, "read",
                                  error);
    }
  }
  return TRUE;
}

void bosonoga_use_variable(BosonogaParser *parser,
                           const BosonogaVariable *variable) {
  const guint index = (guint)(variable - &g_array_index(parser->variables,
                                                        BosonogaVariable, 0));
  g_array_index(parser->variables, BosonogaVariable, index)
      .declaration.is_used = TRUE;
}

void bosonoga_use_function(BosonogaParser *parser, const BosonogaToken *token,
                           guint function) {
  if (g_array_index(parser->functions, BosonogaFunction, function).is_var) {
    bosonoga_use_variable(parser, bosonoga_find_variable(parser, token));
    return;
  }
  // A function that only calls itself is not used.
  if (parser->in_function && parser->function == function) {
    return;
  }
  g_array_index(parser->function_declarations, BosonogaDeclaration, function)
      .is_used = TRUE;
}
