
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "vm.h"
#include "value.h"
#include "debug.h"

#define DISPATCH() goto *dispatch[bc_op(*ip)]
#define NEXT()     goto *dispatch[bc_op(*(++ip))]

#define ERR(msg, ...)                            \
    int line = fn->line_info[ip - fn->ins];      \
    ErrInfo info = { fn->chunk_name, line, -1 }; \
    err_run(L, &info, msg, __VA_ARGS__);

#define ERR_UNOP(msg, l)      \
    char *t = type_name((l)); \
    ERR("attempt to " msg " %s value", t)

#define ERR_BINOP(msg, l, r)                               \
    char *lt = type_name((l));                             \
    char *rt = type_name((r));                             \
    if (lt == rt) {                                        \
        ERR("attempt to " msg " two %s values", lt)        \
    } else {                                               \
        ERR("attempt to " msg " %s and %s value", lt, rt)  \
    }

#define CHECK_V(msg, l)     if (!is_num((l))) { ERR_UNOP(msg, l) }
#define CHECK_S(msg, l)     if (!is_str((l))) { ERR_UNOP(msg, l) }
#define CHECK_VV(msg, l, r) if (!is_num((l)) || !is_num((r))) { ERR_BINOP(msg, l, r) }
#define CHECK_VN(msg, l, r) if (!is_num((l))) { ERR_BINOP(msg, l, r) }
#define CHECK_NV(msg, l, r) if (!is_num((r))) { ERR_BINOP(msg, l, r) }

// The interpreter is written using computed gotos, which places individual
// branch instructions at the end of each opcode (rather than using a loop with
// a single big branch instruction). The CPU can then perform branch prediction
// at the end of each opcode. This results in faster performance because certain
// opcode pairs are more common than others (e.g., conditional instructions
// followed by JMP).
void execute(State *L) {
    static void *DISPATCH[] = {
#define X(name, nargs) &&OP_ ## name,
        BYTECODE
#undef X
    };
    void **dispatch = DISPATCH;

    uint64_t fn_v = stack_pop(L);
    assert(is_fn(fn_v));
    Fn *fn = v2fn(fn_v);
    print_fn(fn);
    uint64_t *s = L->stack;
    uint64_t *k = fn->k;
    BcIns *ip = &fn->ins[0];

    CallInfo *cs = &L->call_stack[L->num_calls];
    DISPATCH();

OP_NOP:
    NEXT();


    // ---- Storage ----

OP_MOV:
    s[bc_a(*ip)] = s[bc_d(*ip)];
    NEXT();
OP_KPRIM:
    s[bc_a(*ip)] = prim2v(bc_d(*ip));
    NEXT();
OP_KINT:
    s[bc_a(*ip)] = n2v((double) bc_d(*ip));
    NEXT();
OP_KNUM:
OP_KSTR:
OP_KFN:
    s[bc_a(*ip)] = k[bc_d(*ip)];
    NEXT();
OP_KNIL:
    for (int n = bc_a(*ip); n <= bc_d(*ip); n++) {
        s[n] = VAL_NIL;
    }
    NEXT();


    // ---- Arithmetic ----

OP_NEG:
    CHECK_V("negate", s[bc_d(*ip)])
    s[bc_a(*ip)] = n2v(-v2n(s[bc_d(*ip)]));
    NEXT();

OP_ADDVV:
    CHECK_VV("add", s[bc_b(*ip)], s[bc_c(*ip)])
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) + v2n(s[bc_c(*ip)]));
    NEXT();
OP_ADDVN:
    CHECK_VN("add", s[bc_b(*ip)], k[bc_c(*ip)])
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) + v2n(k[bc_c(*ip)]));
    NEXT();

OP_SUBVV:
    CHECK_VV("subtract", s[bc_b(*ip)], s[bc_c(*ip)])
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) - v2n(s[bc_c(*ip)]));
    NEXT();
OP_SUBVN:
    CHECK_VN("subtract", s[bc_b(*ip)], k[bc_c(*ip)])
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) - v2n(k[bc_c(*ip)]));
    NEXT();
OP_SUBNV:
    CHECK_NV("subtract", k[bc_b(*ip)], s[bc_c(*ip)])
    s[bc_a(*ip)] = n2v(v2n(k[bc_b(*ip)]) - v2n(s[bc_c(*ip)]));
    NEXT();

OP_MULVV:
    CHECK_VV("multiply", s[bc_b(*ip)], s[bc_c(*ip)])
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) * v2n(s[bc_c(*ip)]));
    NEXT();
OP_MULVN:
    CHECK_VN("multiply", s[bc_b(*ip)], k[bc_c(*ip)])
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) * v2n(k[bc_c(*ip)]));
    NEXT();

OP_DIVVV:
    CHECK_VV("divide", s[bc_b(*ip)], s[bc_c(*ip)])
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) / v2n(s[bc_c(*ip)]));
    NEXT();
OP_DIVVN:
    CHECK_VN("divide", s[bc_b(*ip)], k[bc_c(*ip)])
    s[bc_a(*ip)] = n2v(v2n(s[bc_b(*ip)]) / v2n(k[bc_c(*ip)]));
    NEXT();
OP_DIVNV:
    CHECK_NV("divide", k[bc_b(*ip)], s[bc_c(*ip)])
    s[bc_a(*ip)] = n2v(v2n(k[bc_b(*ip)]) / v2n(s[bc_c(*ip)]));
    NEXT();

OP_MODVV:
    CHECK_VV("modulo", s[bc_b(*ip)], s[bc_c(*ip)])
    s[bc_a(*ip)] = n2v(fmod(v2n(s[bc_b(*ip)]), v2n(s[bc_c(*ip)])));
    NEXT();
OP_MODVN:
    CHECK_VN("modulo", s[bc_b(*ip)], k[bc_c(*ip)])
    s[bc_a(*ip)] = n2v(fmod(v2n(s[bc_b(*ip)]), v2n(k[bc_c(*ip)])));
    NEXT();
OP_MODNV:
    CHECK_NV("modulo", k[bc_b(*ip)], s[bc_c(*ip)])
    s[bc_a(*ip)] = n2v(fmod(v2n(k[bc_b(*ip)]), v2n(s[bc_c(*ip)])));
    NEXT();

OP_POW:
    CHECK_VV("perform exponentiation on", s[bc_b(*ip)], s[bc_c(*ip)])
    s[bc_a(*ip)] = n2v(pow(v2n(s[bc_b(*ip)]), v2n(s[bc_c(*ip)])));
    NEXT();

OP_CONCAT: {
    size_t len = 0;
    for (uint8_t i = bc_b(*ip); i <= bc_c(*ip); i++) {
        CHECK_S("concatenate", s[i])
        Str *str = v2str(s[i]);
        len += str->len;
    }
    Str *v = str_new(L, len);
    char *concat = str_val(v);
    for (uint8_t i = bc_b(*ip); i <= bc_c(*ip); i++) {
        Str *str = v2str(s[i]);
        memcpy(concat, str_val(str), str->len);
        concat += str->len;
    }
    s[bc_a(*ip)] = str2v(v);
    NEXT();
}


    // ---- Conditions ----

OP_NOT:
    s[bc_a(*ip)] = compares_true(s[bc_d(*ip)]) ? VAL_TRUE : VAL_FALSE;
    NEXT();

    // Comparison instructions are always followed by a 'BC_JMP' instruction.
    // We skip the jump instruction (by incrementing 'ip') if the condition is
    // *false*.

OP_IST:
    if (!compares_true(s[bc_d(*ip)])) { ip++; }
    NEXT();
OP_ISTC:
    if (!compares_true(s[bc_d(*ip)])) {
        ip++;
    } else {
        s[bc_a(*ip)] = s[bc_d(*ip)];
    }
    NEXT();

OP_ISF:
    if (compares_true(s[bc_d(*ip)])) { ip++; }
    NEXT();
OP_ISFC:
    if (compares_true(s[bc_d(*ip)])) {
        ip++;
    } else {
        s[bc_a(*ip)] = s[bc_d(*ip)];
    }
    NEXT();

OP_EQVV:
    if (s[bc_a(*ip)] != s[bc_d(*ip)]) { ip++; }
    NEXT();
OP_EQVP:
    if (s[bc_a(*ip)] != prim2v(bc_d(*ip))) { ip++; }
    NEXT();
OP_EQVN:
    if (s[bc_a(*ip)] != k[bc_d(*ip)]) { ip++; }
    NEXT();
OP_EQVS:
    if (!str_eq(v2str(s[bc_a(*ip)]), v2str(k[bc_d(*ip)]))) { ip++; }
    NEXT();

OP_NEQVV:
    if (s[bc_a(*ip)] == s[bc_d(*ip)]) { ip++; }
    NEXT();
OP_NEQVP:
    if (s[bc_a(*ip)] == prim2v(bc_d(*ip))) { ip++; }
    NEXT();
OP_NEQVN:
    if (s[bc_a(*ip)] == k[bc_d(*ip)]) { ip++; }
    NEXT();
OP_NEQVS:
    if (str_eq(v2str(s[bc_a(*ip)]), v2str(k[bc_d(*ip)]))) { ip++; }
    NEXT();

OP_LTVV:
    CHECK_VV("compare less than", s[bc_a(*ip)], s[bc_d(*ip)])
    if (v2n(s[bc_a(*ip)]) >= v2n(s[bc_d(*ip)])) { ip++; }
    NEXT();
OP_LTVN:
    CHECK_VN("compare less than", s[bc_a(*ip)], k[bc_d(*ip)])
    if (v2n(s[bc_a(*ip)]) >= v2n(k[bc_d(*ip)])) { ip++; }
    NEXT();

OP_LEVV:
    CHECK_VV("compare less than or equal", s[bc_a(*ip)], s[bc_d(*ip)])
    if (v2n(s[bc_a(*ip)]) > v2n(s[bc_d(*ip)])) { ip++; }
    NEXT();
OP_LEVN:
    CHECK_VN("compare less than or equal", s[bc_a(*ip)], k[bc_d(*ip)])
    if (v2n(s[bc_a(*ip)]) > v2n(k[bc_d(*ip)])) { ip++; }
    NEXT();

OP_GTVV:
    CHECK_VV("compare greater than", s[bc_a(*ip)], s[bc_d(*ip)])
    if (v2n(s[bc_a(*ip)]) <= v2n(s[bc_d(*ip)])) { ip++; }
    NEXT();
OP_GTVN:
    CHECK_VN("compare greater than", s[bc_a(*ip)], k[bc_d(*ip)])
    if (v2n(s[bc_a(*ip)]) <= v2n(k[bc_d(*ip)])) { ip++; }
    NEXT();

OP_GEVV:
    CHECK_VV("compare greater than or equal", s[bc_a(*ip)], s[bc_d(*ip)])
    if (v2n(s[bc_a(*ip)]) < v2n(s[bc_d(*ip)])) { ip++; }
    NEXT();
OP_GEVN:
    CHECK_VN("compare greater than or equal", s[bc_a(*ip)], k[bc_d(*ip)])
    if (v2n(s[bc_a(*ip)]) < v2n(k[bc_d(*ip)])) { ip++; }
    NEXT();


    // ---- Control Flow ----

OP_JMP:
    ip += (int) bc_e(*ip) - JMP_BIAS;
    DISPATCH();

OP_CALL: {
    CallInfo *c = &cs[L->num_calls++];
    if (L->num_calls >= L->max_calls) {
        L->call_stack = cs = mem_realloc(L, cs,
                L->max_calls * sizeof(CallInfo),
                L->max_calls * sizeof(CallInfo) * 2);
        L->max_calls *= 2;
    }
    c->fn = fn;
    c->ip = ip;
    c->s = s;
    c->num_rets = bc_c(*ip);
    fn = v2fn(s[bc_a(*ip)]);
    ip = &fn->ins[0];
    s = &s[bc_a(*ip) + 1];
    k = fn->k;
    DISPATCH();
}

OP_RET0: {
    if (L->num_calls == 0) {
        goto end;
    }
    CallInfo *c = &cs[--L->num_calls];
    for (int i = -1; i < c->num_rets - 1; i++) { // Return values start at s[-1]
        s[i] = VAL_NIL;
    }
    fn = c->fn;
    ip = c->ip;
    s = c->s;
    k = fn->k;
    NEXT();
}

OP_RET1: {
    if (L->num_calls == 0) {
        goto end;
    }
    CallInfo *c = &cs[--L->num_calls];
    s[-1] = s[bc_d(*ip)]; // Return values start at s[-1]
    for (int i = 0; i < c->num_rets - 1; i++) { // Set the rest to nil
        s[i] = VAL_NIL;
    }
    fn = c->fn;
    ip = c->ip;
    s = c->s;
    k = fn->k;
    NEXT();
}

OP_RET: {
    if (L->num_calls == 0) {
        goto end;
    }
    CallInfo *c = &cs[--L->num_calls];
    int i = 0;
    while (i < bc_d(*ip)) { // Copy return values
        s[-1 + i] = s[bc_a(*ip) + i]; // Return values start at s[-1]
        i++;
    }
    while (i < c->num_rets) { // Set remaining to nil
        s[-1 + i] = VAL_NIL; // Return values start at s[-1]
        i++;
    }
    fn = c->fn;
    ip = c->ip;
    s = c->s;
    k = fn->k;
    NEXT();
}

end:
    printf("first stack: %g\n", v2n(s[1]));
}
