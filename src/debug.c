
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

static void print_fn_name(Fn *f) {
    if (f->name) {
        printf("%.*s", (int) f->name->len, str_val(f->name));
    } else {
        printf("<unknown>");
    }
    if (f->chunk_name) {
        printf("@%s", f->chunk_name);
    } else {
        printf("@<unknown>");
    }
    if (f->start_line >= 1) {
        printf(":%d", f->start_line);
    }
    if (f->end_line >= f->start_line) {
        printf("-%d", f->end_line);
    }
}

static void quote_ch(char ch) {
    switch (ch) {
    case '\\': printf("\\\\"); break;
    case '\"': printf("\\\""); break;
    case '\'': printf("\\'"); break;
    case '\a': printf("\\a"); break;
    case '\b': printf("\\b"); break;
    case '\f': printf("\\f"); break;
    case '\n': printf("\\n"); break;
    case '\r': printf("\\r"); break;
    case '\t': printf("\\t"); break;
    case '\v': printf("\\v"); break;
    case 0:    printf("\\0"); break;
    default:
        if (iscntrl(ch)) {
            printf("\\%03o", ch);
        } else {
            printf("%c", ch);
        }
        break;
    }
}

static void quote_str(char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        quote_ch(s[i]);
    }
}

static void print_ins(Fn *f, int idx, const BcIns *ins) {
    printf("%.4d", idx);
    int op = bc_op(*ins);
    DebugInfo info = BC_DEBUG_INFO[op];
    printf("\t%s", info.name);
    if (op == BC_JMP) {
        printf("\t=> %.4d\n", idx + (int) bc_e(*ins) - JMP_BIAS);
        return;
    }
    switch (info.num_args) {
        case 1: printf("\t%d\t\t", bc_e(*ins)); break;
        case 2: printf("\t%d\t%d\t", bc_a(*ins), bc_d(*ins)); break;
        case 3: printf("\t%d\t%d\t%d", bc_a(*ins), bc_b(*ins), bc_c(*ins)); break;
        default: break;
    }
    Str *s;
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
    case BC_KSTR: case BC_EQVS: case BC_NEQVS:
        s = v2str(f->k[bc_d(*ins)]);
        printf("\t; \"");
        quote_str(str_val(s), s->len);
        printf("\"");
        break;
    case BC_KFN:
        printf("\t; ");
        print_fn_name(v2fn(f->k[bc_d(*ins)]));
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

static void print_bc(Fn *f) {
    for (int idx = 0; idx < f->num_ins; idx++) {
        print_ins(f, idx, &f->ins[idx]);
    }
}

void print_fn(Fn *f) {
    printf("-- ");
    print_fn_name(f);
    printf(" --\n");
    print_bc(f);
    for (int i = 0; i < f->num_k; i++) {
        if (is_fn(f->k[i])) {
            Fn *f2 = v2fn(f->k[i]);
            printf("\n");
            print_fn(f2);
        }
    }
}
