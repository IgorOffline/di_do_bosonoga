#ifndef DARIA_INTERPRETER_H_
#define DARIA_INTERPRETER_H_

#include <glib.h>

// Runs program, an array of DariaOp from daria_parse, and writes its output
// to stdout.
void daria_run(const GArray *program);

#endif  // DARIA_INTERPRETER_H_
