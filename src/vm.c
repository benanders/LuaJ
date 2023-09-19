
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "vm.h"
#include "value.h"
#include "debug.h"

#define DISPATCH() goto *dispatch[bc_op(*ip)]
#define NEXT()     goto *dispatch[bc_op(*(++ip))]

static inline void check_num(State *L, uint64_t v) {
    if (!is_num(v)) {
        char *kind = type_name(v);
        err_run(L, NULL, "attempt to perform arithmetic on %s value", kind);
    }
}

// The interpreter is written using computed gotos, which places individual
// branch instructions at the end of each opcode (rather than using a loop with
// a single big branch instruction). The CPU can then perform branch prediction
// individually at the end of each opcode. This results in faster performance
// because certain opcode pairs are more common than others (e.g., conditional
// instructions followed by JMP).
void execute(State *L) {
    static void *DISPATCH[] = {
#define X(name, nargs) &&OP_ ## name,
        BYTECODE
#undef X
    };
    void **dispatch = DISPATCH;

    uint64_t v = stack_pop(L);
    assert(is_fn(v));
    Fn *fn = v2fn(v);
    print_fn(L, fn);

    uint64_t *s = L->stack;
    uint64_t *k = fn->k;
    BcIns *ip = &fn->ins[0];
    DISPATCH();

OP_NOP:
    NEXT();


    // ---- Storage ----

OP_MOV:
    s[bc_a(*ip)] = s[bc_d(*ip)];
    NEXT();
OP_KINT:
    s[bc_a(*ip)] = n2v((double) bc_d(*ip));
    NEXT();
OP_KNUM:
    s[bc_a(*ip)] = k[bc_d(*ip)];
    NEXT();
OP_KPRIM:
    s[bc_a(*ip)] = prim2v(bc_d(*ip));
    NEXT();


    // ---- Arithmetic ----

    // TODO: type checking
OP_NEG:
    check_num(L, s[bc_d(*ip)]);
    s[bc_a(*ip)] = n2v(-v2n(s[bc_d(*ip)]));
    NEXT();

OP_ADDVV:
    check_num(L, s[bc_b(*ip)]); check_num(L, s[bc_c(*ip)]);
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) + v2n(s[bc_c(*ip)]));
    NEXT();
OP_ADDVN:
    check_num(L, s[bc_b(*ip)]);
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) + v2n(k[bc_c(*ip)]));
    NEXT();

OP_SUBVV:
    check_num(L, s[bc_b(*ip)]); check_num(L, s[bc_c(*ip)]);
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) - v2n(s[bc_c(*ip)]));
    NEXT();
OP_SUBVN:
    check_num(L, s[bc_b(*ip)]);
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) - v2n(k[bc_c(*ip)]));
    NEXT();
OP_SUBNV:
    check_num(L, s[bc_c(*ip)]);
    s[bc_a(*ip)] = n2v(v2n(k[bc_b(*ip)]) - v2n(s[bc_c(*ip)]));
    NEXT();

OP_MULVV:
    check_num(L, s[bc_b(*ip)]); check_num(L, s[bc_c(*ip)]);
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) * v2n(s[bc_c(*ip)]));
    NEXT();
OP_MULVN:
    check_num(L, s[bc_b(*ip)]);
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) * v2n(k[bc_c(*ip)]));
    NEXT();

OP_DIVVV:
    check_num(L, s[bc_b(*ip)]); check_num(L, s[bc_c(*ip)]);
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) / v2n(s[bc_c(*ip)]));
    NEXT();
OP_DIVVN:
    check_num(L, s[bc_b(*ip)]);
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) / v2n(k[bc_c(*ip)]));
    NEXT();
OP_DIVNV:
    check_num(L, s[bc_c(*ip)]);
    s[bc_a(*ip)] = n2v(v2n(k[bc_b(*ip)]) / v2n(s[bc_c(*ip)]));
    NEXT();

OP_MODVV:
    check_num(L, s[bc_b(*ip)]); check_num(L, s[bc_c(*ip)]);
    s[bc_a(*ip)] = n2v(fmod(v2n(s[bc_b(*ip)]), v2n(s[bc_c(*ip)])));
    NEXT();
OP_MODVN:
    check_num(L, s[bc_b(*ip)]);
    s[bc_a(*ip)] = n2v(fmod(v2n(s[bc_b(*ip)]), v2n(k[bc_c(*ip)])));
    NEXT();
OP_MODNV:
    check_num(L, s[bc_c(*ip)]);
    s[bc_a(*ip)] = n2v(fmod(v2n(k[bc_b(*ip)]), v2n(s[bc_c(*ip)])));
    NEXT();

OP_POW:
    check_num(L, s[bc_b(*ip)]); check_num(L, s[bc_c(*ip)]);
    s[bc_a(*ip)] = n2v(pow(v2n(s[bc_b(*ip)]), v2n(s[bc_c(*ip)])));
    NEXT();


    // ---- Control Flow ----

OP_RET0:
    printf("first stack: %g\n", v2n(s[1]));
}
