
#include <assert.h>

#include "vm.h"
#include "value.h"
#include "debug.h"

void execute(State *L) {
    uint64_t v = stack_pop(L);
    assert(is_fn(v));
    Fn *fn = v2fn(v);
    print_fn(L, fn);

//    uint64_t *base = L->stack;
    err_run(L, NULL, "test runtime error");
}
