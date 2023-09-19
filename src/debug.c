
#include <stdio.h>

#include "debug.h"

typedef struct {
    char *name;
    int num_args;
} DebugInfo;

static DebugInfo BC_DEBUG_INFO[] = {
    { "NOP", 0 },
    { "KINT", 2 }, { "KNUM", 2 }, { "KPRIM", 2 }, { "KFN", 2 }, { "KNIL", 2 },
    { "MOV", 2 }, { "NEG", 2 },
    { "ADDVV", 3 }, { "ADDVN", 3 },
    { "SUBVV", 3 }, { "SUBVN", 3 }, { "SUBNV", 3 },
    { "MULVV", 3 }, { "MULVN", 3 },
    { "DIVVV", 3 }, { "DIVVN", 3 }, { "DIVNV", 3 },
    { "MODVV", 3 }, { "MODVN", 3 }, { "MODNV", 3 },
    { "POW", 3 }, { "CAT", 3 },
    { "RET0", 0 },
};

static void print_ins(State *L, Fn *f, int idx, const BcIns *ins) {
    printf("%.4d", idx);
    int op = bc_op(*ins);
    DebugInfo info = BC_DEBUG_INFO[op];
    printf("\t%s", info.name);
    switch (info.num_args) {
        case 1: printf("\t%d\t\t", bc_e(*ins)); break;
        case 2: printf("\t%d\t%d\t", bc_a(*ins), bc_d(*ins)); break;
        case 3: printf("\t%d\t%d\t%d", bc_a(*ins), bc_b(*ins), bc_c(*ins)); break;
        default: break;
    }
    switch (op) {
    case BC_KNUM:
        printf("\t; %g", v2n(f->k[bc_d(*ins)]));
        break;
    case BC_KPRIM:
        printf("\t; ");
        switch (bc_d(*ins)) {
            case TAG_NIL:   printf("nil"); break;
            case TAG_TRUE:  printf("true"); break;
            case TAG_FALSE: printf("false"); break;
        }
        break;
    case BC_SUBNV: case BC_DIVNV: case BC_MODNV:
        printf("\t; %g", v2n(f->k[bc_b(*ins)]));
        break;
    case BC_ADDVN: case BC_SUBVN: case BC_MULVN: case BC_DIVVN: case BC_MODVN:
        printf("\t; %g", v2n(f->k[bc_c(*ins)]));
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
    printf("-- func --\n");
    print_bc(L, f);
    for (int i = 0; i < f->num_k; i++) {
        if (is_fn(f->k[i])) {
            Fn *f2 = v2fn(f->k[i]);
            printf("\n");
            print_fn(L, f2);
        }
    }
}
