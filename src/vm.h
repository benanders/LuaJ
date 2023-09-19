
#ifndef LUAJ_VM_H
#define LUAJ_VM_H

#include "state.h"

// Expects a function prototype to be on top of the stack; pops the function
// and executes it.
void execute(State *L);

#endif
