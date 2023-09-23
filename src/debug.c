
#include <stdio.h>
#include <ctype.h>

#include "debug.h"

typedef struct {
    char *name;
    int num_args;
} DebugInfo;

static DebugInfo BC_DEBUG_INFO[] = {
#define X(name, num_args) { #name, num_args },
    BYTECODE
#undef X
};

static void print_ins(State *L, Fn *f, int idx, const BcIns *ins) {
    printf("%.4d", idx);
    int op = bc_op(*ins);
    DebugInfo info = BC_DEBUG_INFO[op];
    printf("\t%s", info.name);
    if (op == BC_JMP) {
        printf("\t=> %.4d\n", idx + (int) bc_e(*ins) - JMP_BIAS);
        return;
    }
    switch (info.num_args) {
        case 1: printf("\t%d\t\t", bc_d(*ins)); break;
        case 2: printf("\t%d\t%d\t", bc_a(*ins), bc_d(*ins)); break;
        case 3: printf("\t%d\t%d\t%d", bc_a(*ins), bc_b(*ins), bc_c(*ins)); break;
        default: break;
    }
    switch (op) {
    case BC_KNUM:
        printf("\t; %g", v2n(f->k[bc_d(*ins)]));
        break;
    case BC_KPRIM: case BC_EQVP: case BC_NEQVP:
        printf("\t; ");
        switch (bc_d(*ins)) {
            case TAG_NIL:   printf("nil"); break;
            case TAG_TRUE:  printf("true"); break;
            case TAG_FALSE: printf("false"); break;
        }
        break;
    case BC_KSTR: case BC_KFN: case BC_EQVS: case BC_NEQVS:
        printf("\t; %s", print_val(L, f->k[bc_d(*ins)]));
        break;
    case BC_SUBNV: case BC_DIVNV: case BC_MODNV:
        printf("\t; %g", v2n(f->k[bc_b(*ins)]));
        break;
    case BC_ADDVN: case BC_SUBVN: case BC_MULVN: case BC_DIVVN: case BC_MODVN:
        printf("\t; %g", v2n(f->k[bc_c(*ins)]));
        break;
    case BC_EQVN: case BC_NEQVN:
    case BC_LTVN: case BC_LEVN: case BC_GTVN: case BC_GEVN:
        printf("\t; %g", v2n(f->k[bc_d(*ins)]));
        break;
    default: break;
    }
    printf("\n");
}

static void print_bc(State *L, Fn *f) {
    for (int idx = 0; idx < f->num_ins; idx++) {
        print_ins(L, f, idx, &f->ins[idx]);
    }
}

void print_fn(State *L, Fn *f) {
    printf("-- %s --\n", print_val(L, fn2v(f)));
    print_bc(L, f);
    for (int i = 0; i < f->num_k; i++) {
        if (is_fn(f->k[i])) {
            Fn *f2 = v2fn(f->k[i]);
            printf("\n");
            print_fn(L, f2);
        }
    }
}
