
#ifndef LUAJ_PARSER_H
#define LUAJ_PARSER_H

#include "state.h"
#include "reader.h"

// Compiles the source code provided by the reader 'r' and pushes a function
// prototype object onto the top of the stack.
void parse(State *L, Reader *r);

#endif
