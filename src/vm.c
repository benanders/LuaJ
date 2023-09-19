
#include <assert.h>

#include "vm.h"
#include "value.h"
#include "debug.h"

#define DISPATCH() goto *dispatch[ins_op(*ip)]
#define NEXT()     goto *dispatch[ins_op(*(++ip))]

void execute(State *L) {
//    static void *DISPATCH[] = { // Normal dispatch table (when we're not tracing)
//#define X(name, nargs) &&OP_ ## name,
//        BYTECODE
//#undef X
//    };

    uint64_t v = stack_pop(L);
    assert(is_fn(v));
    Fn *fn = v2fn(v);
    print_fn(L, fn);

    uint64_t *base = L->stack;
    err_run(L, NULL, "test runtime error");
}
