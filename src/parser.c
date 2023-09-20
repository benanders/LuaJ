
#include <assert.h>
#include <string.h>
#include <math.h>

#include "parser.h"
#include "lexer.h"
#include "value.h"

// For instructions associated with an EXPR_RELOC that haven't had a destination
// slot assigned yet.
#define NO_SLOT 0xff

// Used for 'BC_JMP' instructions that have been emitted but haven't had their
// jump target set yet.
#define JMP_NONE (-1)

enum {
    EXPR_PRIM,
    EXPR_NUM,
    EXPR_SLOT,
    EXPR_RELOC,
    EXPR_JMP,
};

typedef struct {
    int t;
    union {
        uint16_t tag; // EXPR_PRIM
        double num;   // EXPR_NUM
        uint8_t slot; // EXPR_SLOT
        int pc;       // EXPR_RELOC, EXPR_JMP
    };
    int true_list, false_list;
} Expr;

typedef struct BlockScope {
    struct BlockScope *outer;
    int first_local;
} BlockScope;

typedef struct FnScope {
    struct FnScope *outer;
    Fn *fn;
    int num_stack, num_locals;
    Str *locals[LUAI_MAXVARS];
    BlockScope *b;
} FnScope;

typedef struct {
    State *L;
    Lexer *l;
    FnScope *f;
} Parser;

static Parser parser_new(State *L, Lexer *l) {
    Parser p = {0};
    p.L = L;
    p.l = l;
    return p;
}

static void expr_new(Expr *e, int t) {
    *e = (Expr) {0};
    e->t = t;
    e->true_list = e->false_list = JMP_NONE;
}

static int emit(Parser *p, BcIns ins) {
    return fn_emit(p->L, p->f->fn, ins);
}

static int emit_k(Parser *p, uint64_t k) {
    int idx = fn_emit_k(p->L, p->f->fn, k);
    if (idx > UINT16_MAX) {
        Token err;
        peek_tk(p->l, &err);
        ErrInfo info = tk2err(&err);
        err_syntax(p->L, &info, "too many constants in function");
    }
    return idx;
}

static void enter_fn(Parser *p, FnScope *f) {
    *f = (FnScope) {0};
    f->outer = p->f;
    p->f = f;
    f->fn = fn_new(p->L);
}

static void exit_fn(Parser *p) {
    assert(p->f);
    emit(p, ins0(BC_RET0));
    p->f = p->f->outer;
}

static void enter_block(Parser *p, BlockScope *b) {
    assert(p->f->num_stack == p->f->num_locals); // No temps on stack
    *b = (BlockScope) {0};
    b->first_local = p->f->num_locals;
    b->outer = p->f->b;
    p->f->b = b;
}

static void exit_block(Parser *p) {
    assert(p->f->b);
    p->f->num_locals = p->f->num_stack = p->f->b->first_local;
    p->f->b = p->f->b->outer;
}

static uint8_t reserve_slot(Parser *p) {
    if (p->f->num_stack >= UINT8_MAX) { // 254 max (0xff is NO_SLOT)
        Token err;
        peek_tk(p->l, &err);
        ErrInfo info = tk2err(&err);
        err_syntax(p->L, &info, "too many local variables in function");
    }
    return p->f->num_stack++;
}

static void def_local(Parser *p, Str *name) {
    p->f->locals[p->f->num_locals++] = name;
}

static void find_var(Parser *p, Expr *e, Str *name) {
    for (int i = p->f->num_locals - 1; i >= 0; i--) { // In reverse
        Str *l = p->f->locals[i];
        if (name->len == l->len &&
                strncmp(str_val(name), str_val(l), l->len) == 0) {
            expr_new(e, EXPR_SLOT);
            e->slot = i;
            return;
        }
    }
    assert(0); // TODO: upvalues and globals
}


// ---- Expressions ----

// Expression results are stored in 'Expr' and only emitted to a stack slot
// when the context of their usage is known (e.g., the expression result is
// assigned to something, called as a function, used as a condition in a loop,
// etc.)
//
// Relocatable expressions reference bytecode instructions that haven't had a
// destination slot assigned yet. For example, a 'BC_ADD' instruction without
// its 'a' parameter set. The destination slot for the expression is set when
// it's used.

enum {
    PREC_MIN,
    PREC_OR,     // or
    PREC_AND,    // and
    PREC_CMP,    // ==, ~=, <, >, <=, >=
    PREC_CONCAT, // ..
    PREC_ADD,    // +, -
    PREC_MUL,    // *, /, %
    PREC_UNARY,  // -, not, #
    PREC_POW,    // ^
};

// The format of each X-entry in the unary operator table is:
//   token, bytecode opcode
#define UNOPS         \
    X('-', BC_NEG)

// The format of each X-entry in the binary operator table is:
//   token, precedence, bytecode opcode, is commutative?, is right associative?
#define BINOPS \
    X('+',    PREC_ADD, BC_ADDVV, 1, 0) \
    X('-',    PREC_ADD, BC_SUBVV, 0, 0) \
    X('*',    PREC_MUL, BC_MULVV, 1, 0) \
    X('/',    PREC_MUL, BC_DIVVV, 0, 0) \
    X('%',    PREC_MUL, BC_MODVV, 0, 0) \
    X('^',    PREC_POW, BC_POW,   0, 1) \
    X(TK_EQ,  PREC_CMP, BC_EQVV,  1, 0) \
    X(TK_NEQ, PREC_CMP, BC_NEQVV, 1, 0) \
    X('<',    PREC_CMP, BC_LTVV,  0, 0) \
    X(TK_LE,  PREC_CMP, BC_LEVV,  0, 0) \
    X('>',    PREC_CMP, BC_GTVV,  0, 0) \
    X(TK_GE,  PREC_CMP, BC_GEVV,  0, 0) \
    X(TK_AND, PREC_AND, BC_NOP,   0, 0) \
    X(TK_OR,  PREC_OR,  BC_NOP,   0, 0)

static int BINOP_PREC[TK_LAST] = {
#define X(tk, prec, _, __, ___) [tk] = prec,
    BINOPS
#undef X
};

static int UNOP_PREC[TK_LAST] = {
#define X(tk, _) [tk] = PREC_UNARY,
    UNOPS
#undef X
};

static int IS_COMMUTATIVE[TK_LAST] = {
#define X(tk, _, __, is_comm, ___) [tk] = is_comm,
    BINOPS
#undef X
};

static int IS_RASSOC[TK_LAST] = {
#define X(tk, _, __, ___, rassoc) [tk] = rassoc,
    BINOPS
#undef X
};

static uint8_t UNOP_BC[TK_LAST] = {
#define X(tk, op) [tk] = op,
    UNOPS
#undef X
};

static uint8_t BINOP_BC[TK_LAST] = {
#define X(tk, _, op, __, ___) [tk] = op,
    BINOPS
#undef X
};

static int INVERT_TK[TK_LAST] = {
    [TK_EQ] = TK_NEQ, [TK_NEQ] = TK_EQ,
    ['<'] = TK_GE, [TK_LE] = '>',
    ['>'] = TK_LE, [TK_GE] = '<',
};

static int INVERT_OP[BC_LAST] = {
    [BC_IST] = BC_ISF, [BC_ISTC] = BC_ISFC,
    [BC_ISF] = BC_IST, [BC_ISFC] = BC_ISTC,
    [BC_EQVV] = BC_NEQVV, [BC_EQVN] = BC_NEQVN, [BC_EQVP] = BC_NEQVP,
    [BC_NEQVV] = BC_EQVV, [BC_NEQVN] = BC_EQVN, [BC_NEQVP] = BC_EQVP,
    [BC_LTVV] = BC_GEVV, [BC_LTVN] = BC_GEVN,
    [BC_LEVV] = BC_GTVV, [BC_LEVN] = BC_GTVN,
    [BC_GTVV] = BC_LEVV, [BC_GTVN] = BC_LEVN,
    [BC_GEVV] = BC_LTVV, [BC_GEVN] = BC_LTVN,
};

static inline int is_int(double n, int *i) {
    lua_number2int(*i, n);
    return n == *i;
}

static void patch_jmp(Parser *p, int jmp, int target) {
    if (jmp == JMP_NONE) {
        return;
    }
    BcIns *ins = &p->f->fn->ins[jmp];
    int offset = target - jmp + JMP_BIAS;
    if (offset >= 1 << 24) { // Max 24-bit value
        Token tk;
        peek_tk(p->l, &tk);
        ErrInfo info = tk2err(&tk);
        err_syntax(p->L, &info, "control structure too long");
    }
    bc_set_e(ins, (uint32_t) offset);
}

static int follow_jmp(Parser *p, int jmp) {
    assert(jmp != JMP_NONE);
    BcIns *ins = &p->f->fn->ins[jmp];
    int delta = (int) bc_e(*ins);
    return jmp + delta - JMP_BIAS;
}

static int emit_jmp(Parser *p) {
    int pc = emit(p, ins0(BC_JMP));
    patch_jmp(p, pc, JMP_NONE);
    return pc;
}

// Is there a jump list associated with the expression value?
static int has_jmp(Expr *e) {
    return e->true_list != JMP_NONE || e->false_list != JMP_NONE;
}

// Add a jump list (or single jump instruction) to a jump list. 'head'
// specifies a jump list. This function puts 'to_add' at the start of the
// 'head' jump list.
static void append_jmp(Parser *p, int *head, int to_add) {
    if (to_add == JMP_NONE) { // Nothing to add
        return;
    }
    if (*head == JMP_NONE) { // 'head' list is empty
        *head = to_add;
        return;
    }
    while (follow_jmp(p, to_add) != JMP_NONE) { // Find end of 'to_add' list
        to_add = follow_jmp(p, to_add);
    }
    patch_jmp(p, to_add, *head);
    *head = to_add;
}

// Discards a value associated with a jump. E.g., in '3 or x', we discard the
// jump associated with '3' (KINT). Returns 1 if there was a value to discard.
static int discard_value(Parser *p, int jmp) {
    BcIns *cond = &p->f->fn->ins[jmp > 0 ? jmp - 1 : 0];
    if (bc_op(*cond) == BC_ISTC || bc_op(*cond) == BC_ISFC) {
        bc_set_op(cond, bc_op(*cond) - 1); // ISTC -> IST or ISFC -> ISF
        bc_set_a(cond, 0);
        return 1;
    } else if (bc_a(*cond) == NO_SLOT) { // RELOC
        *cond = ins0(BC_NOP); // Make the jump unconditional
        return 1;
    } else {
        return 0;
    }
}

static void discard_values(Parser *p, int head) {
    while (head != JMP_NONE) {
        discard_value(p, head);
        head = follow_jmp(p, head);
    }
}

// Patches the destination slot for a value associated with a jump (e.g., in
// 'x and 3'). Returns 1 if there was a value to patch.
static int patch_value(Parser *p, int jmp, uint8_t dst) {
    BcIns *cond = &p->f->fn->ins[jmp > 0 ? jmp - 1 : 0];
    if (dst == NO_SLOT) {
        return discard_value(p, jmp);
    } else if (bc_op(*cond) == BC_ISTC || bc_op(*cond) == BC_ISFC ||
            bc_a(*cond) == NO_SLOT) { // Is there a value to patch?
        bc_set_a(cond, dst);
        return 1;
    } else {
        return 0;
    }
}

// Iterates over the jump list. For jumps in the jump list that don't have a
// value associated with them, patches them to 'jmp_target'. For jumps that
// have an associated value, stores the value into 'dst' and patches the jump
// to 'val_target'.
//
// E.g., in 'a and b == 3 or c + 3', the 'a' and 'c' conditions (ISFC/ISTC/ADD)
// have their first operand set to 'dst' and have their jumps patched to
// 'val_target'. The 'b == 3' condition (EQVN) has its jump patched to
// 'jmp_target'.
static void patch_jmps_and_vals(
        Parser *p,
        int head,
        int jmp_target,
        uint8_t dst,
        int val_target) {
    while (head != JMP_NONE) {
        int next = follow_jmp(p, head);
        if (patch_value(p, head, dst)) {
            patch_jmp(p, head, val_target);
        } else {
            patch_jmp(p, head, jmp_target);
        }
        head = next;
    }
}

static void patch_jmps(Parser *p, int head, int target) {
    patch_jmps_and_vals(p, head, target, NO_SLOT, target);
}

// Checks to see if all jumps in the jump list have a value associated with
// them, or if any are just pure conditions.
//
// E.g., the true and false jump lists for 'a and b + 3 or c' all have values
// associated with them. 'a and b == 3 or c' doesn't.
static int needs_fall_through(Parser *p, int head) {
    while (head != JMP_NONE) {
        BcIns *cond = &p->f->fn->ins[head > 0 ? head - 1 : 0];
        uint8_t op = bc_op(*cond);
        if (!(op == BC_ISTC || op == BC_ISFC || bc_a(*cond) == NO_SLOT)) {
            return 1;
        }
        head = follow_jmp(p, head);
    }
    return 0;
}

// Stores the result of an expression into a specific stack slot.
static void to_slot(Parser *p, Expr *e, uint8_t dst) {
    int i;
    switch (e->t) {
    case EXPR_PRIM:
        emit(p, ins2(BC_KPRIM, dst, e->tag));
        break;
    case EXPR_NUM:
        if (is_int(e->num, &i) && i <= UINT16_MAX) {
            emit(p, ins2(BC_KINT, dst, (uint16_t) i));
        } else {
            uint16_t idx = emit_k(p, n2v(e->num));
            emit(p, ins2(BC_KNUM, dst, idx));
        }
        break;
    case EXPR_SLOT:
        if (dst != e->slot) {
            emit(p, ins2(BC_MOV, dst, e->slot));
        }
        break;
    case EXPR_RELOC:
        bc_set_a(&p->f->fn->ins[e->pc], dst);
        break;
    default: break;
    }
    if (e->t == EXPR_JMP) {
        append_jmp(p, &e->true_list, e->pc);
    }
    if (has_jmp(e)) {
        int true_case = JMP_NONE, false_case = JMP_NONE;
        if (needs_fall_through(p, e->true_list) ||
                needs_fall_through(p, e->false_list)) {
            int before = (e->t != EXPR_JMP) ? emit_jmp(p) : JMP_NONE;
            false_case = emit(p, ins2(BC_KPRIM, dst, TAG_FALSE));
            int middle = emit_jmp(p);
            true_case = emit(p, ins2(BC_KPRIM, dst, TAG_TRUE));
            int after = p->f->fn->num_ins;
            patch_jmp(p, before, after);
            patch_jmp(p, middle, after);
        }
        int after = p->f->fn->num_ins;
        patch_jmps_and_vals(p, e->true_list, true_case, dst, after);
        patch_jmps_and_vals(p, e->false_list, false_case, dst, after);
    }
    expr_new(e, EXPR_SLOT);
    e->slot = dst;
}

// When calling this function, we know we won't be using 'e's stack slot again.
// If 'e' is at the top of the stack, we can re-use it.
static void free_expr_slot(Parser *p, Expr *e) {
    if (e->t == EXPR_SLOT && e->slot >= p->f->num_locals) {
        p->f->num_stack--;
        assert(e->slot == p->f->num_stack); // Make sure we freed the stack top
    }
}

// Stores the result of an expression into the next available stack slot (e.g.,
// when assigning a local).
static uint8_t to_next_slot(Parser *p, Expr *e) {
    free_expr_slot(p, e);
    uint8_t dst = reserve_slot(p);
    to_slot(p, e, dst);
    return dst;
}

// Stores the result of an expression into any stack slot. In practice, this
// means expressions already allocated a slot aren't moved, and everything else
// is allocated a new slot.
static uint8_t to_any_slot(Parser *p, Expr *e) {
    if (e->t == EXPR_SLOT) {
        to_slot(p, e, e->slot);
        return e->slot;
    }
    return to_next_slot(p, e);
}

static uint8_t inline_uint8_num(Parser *p, Expr *e) {
    if (e->t == EXPR_NUM && p->f->fn->num_k <= UINT8_MAX) {
        return (uint8_t) emit_k(p, n2v(e->num));
    } else {
        return to_any_slot(p, e);
    }
}

static uint16_t inline_uint16_num(Parser *p, Expr *e) {
    if (e->t == EXPR_NUM) {
        int idx = emit_k(p, n2v(e->num));
        assert(idx <= UINT16_MAX);
        return (uint16_t) idx;
    } else {
        return to_any_slot(p, e);
    }
}

static uint16_t inline_uint16_prim_num(Parser *p, Expr *e) {
    if (e->t == EXPR_PRIM) {
        return e->tag;
    } else {
        return inline_uint16_num(p, e);
    }
}

static int fold_unop(int op, Expr *l) {
    switch (op) {
    case '-':
        if (l->t != EXPR_NUM) {
            return 0;
        }
        l->num = -l->num;
        return 1;
    default: assert(0); // TODO
    }
}

static void emit_unop(Parser *p, int op, Expr *l) {
    if (fold_unop(op, l)) {
        return;
    }
    uint8_t d = to_any_slot(p, l); // Must be in a slot
    free_expr_slot(p, l);
    expr_new(l, EXPR_RELOC);
    l->pc = emit(p, ins2(UNOP_BC[op], NO_SLOT, d));
}

static void invert_cond(Parser *p, int jmp) {
    BcIns *cond = &p->f->fn->ins[jmp - 1];
    int inverted = INVERT_OP[bc_op(*cond)];
    bc_set_op(cond, inverted);
}

// Emits a branch on the "truthiness" of 'l' and adds this jump to 'l's false
// jump list. Patches the expression's true jump list to the instruction after
// the emitted branch.
static void emit_and_left(Parser *p, Expr *l) {
    int to_add;
    switch (l->t) {
    case EXPR_PRIM:
        if (l->tag == TAG_FALSE || l->tag == TAG_NIL) {
            // 'false and x' always evaluates to false
            to_slot(p, l, NO_SLOT); // Discard values
            to_add = emit_jmp(p);   // Unconditional jump to the false case
            break;
        } // 'true and x' always evaluates to 'x' -> fall through...
    case EXPR_NUM: // '3 and x' always evaluates to 'x'
        to_add = JMP_NONE;
        break;
    case EXPR_JMP: // Branch already emitted
        invert_cond(p, l->pc);
        to_add = l->pc;
        break;
    default:
        emit(p, ins2(BC_ISFC, NO_SLOT, to_any_slot(p, l)));
        to_add = emit_jmp(p);
        free_expr_slot(p, l);
        break;
    }
    append_jmp(p, &l->false_list, to_add);
    int next = p->f->fn->num_ins;
    patch_jmps(p, l->true_list, next);
    l->true_list = JMP_NONE;
}

// Emits a branch on the "truthiness" of 'l' and patches the expression's false
// jump list to the instruction after.
static void emit_or_left(Parser *p, Expr *l) {
    int to_add;
    switch (l->t) {
    case EXPR_PRIM:
        if (l->tag == TAG_FALSE || l->tag == TAG_NIL) {
            // 'false or x' always evaluates to 'x'
            to_add = JMP_NONE;
        } // 'true and x' always evaluates to 'x' -> fall through...
    case EXPR_NUM: // '3 or x' always evaluates to '3'
        to_slot(p, l, NO_SLOT); // Discard values
        to_add = emit_jmp(p);   // Unconditional jump to the true case
        break;
    case EXPR_JMP: // Branch already emitted
        to_add = l->pc;
        break;
    default:
        emit(p, ins2(BC_ISTC, NO_SLOT, to_any_slot(p, l)));
        to_add = emit_jmp(p);
        free_expr_slot(p, l);
        break;
    }
    append_jmp(p, &l->true_list, to_add);
    int next = p->f->fn->num_ins;
    patch_jmps(p, l->false_list, next);
    l->false_list = JMP_NONE;
}

static void emit_binop_left(Parser *p, int op, Expr *l) {
    switch (op) {
    case '+': case '-': case '*': case '/': case '%': case '^':
    case '<': case TK_LE: case '>': case TK_GE:
        if (l->t != EXPR_NUM) {
            to_any_slot(p, l);
        }
        break;
    case TK_EQ: case TK_NEQ:
        if (l->t != EXPR_NUM && l->t != EXPR_PRIM) {
            to_any_slot(p, l);
        }
        break;
    case TK_AND: emit_and_left(p, l); break;
    case TK_OR:  emit_or_left(p, l); break;
    default: assert(0); // TODO: remaining binops
    }
}

static int fold_arith(int op, Expr *l, Expr r) {
    if (l->t != EXPR_NUM || r.t != EXPR_NUM) {
        return 0;
    }
    double a = l->num, b = r.num, c;
    switch (op) {
        case '+': c = a + b; break;
        case '-': c = a - b; break;
        case '*': c = a * b; break;
        case '/': c = a / b; break;
        case '%': c = fmod(a, b); break;
        case '^': c = pow(a, b);  break;
        default:  UNREACHABLE();
    }
    expr_new(l, EXPR_NUM);
    l->num = c;
    return 1;
}

static void emit_arith(Parser *p, int op, Expr *l, Expr r) {
    if (fold_arith(op, l, r)) {
        return;
    }
    Expr *ll = l, *rr = &r;
    if (IS_COMMUTATIVE[op] && ll->t != EXPR_SLOT) {
        ll = &r, rr = l; // Constant on the right
    }
    uint8_t b, c;
    if (op == '^') {
        c = to_any_slot(p, rr);
        b = to_any_slot(p, ll);
    } else {
        c = inline_uint8_num(p, rr);
        b = inline_uint8_num(p, ll);
    }
    if (b > c) { // Free top slot first
        free_expr_slot(p, ll);
        free_expr_slot(p, rr);
    } else {
        free_expr_slot(p, rr);
        free_expr_slot(p, ll);
    }
    int bc = BINOP_BC[op] + (rr->t == EXPR_NUM) + (ll->t == EXPR_NUM) * 2;
    expr_new(l, EXPR_RELOC);
    l->pc = emit(p, ins3(bc, NO_SLOT, b, c));
}

static int fold_eq(int op, Expr *l, Expr r) {
    if (l->t >= EXPR_SLOT || r.t >= EXPR_SLOT) {
        return 0;
    }
    int k;
    if (l->t == EXPR_NUM && r.t == EXPR_NUM) {
        k = (op == TK_NEQ) ^ (l->num == r.num);
    } else if (l->t == EXPR_PRIM && r.t == EXPR_PRIM) {
        k = (op == TK_NEQ) ^ (l->tag == r.tag);
    } else { // One a prim and the other a num
        uint16_t tag = l->t == EXPR_PRIM ? l->tag : r.tag;
        k = (op == TK_NEQ) ^ (tag == TAG_TRUE);
    }
    expr_new(l, EXPR_PRIM);
    l->tag = k ? TAG_TRUE : TAG_FALSE;
    return 1;
}

static void emit_eq(Parser *p, int op, Expr *l, Expr r) {
    if (fold_eq(op, l, r)) {
        return;
    }
    Expr *ll = l, *rr = &r;
    if (ll->t != EXPR_SLOT) {
        ll = &r, rr = l; // Constant on the right
    }
    uint16_t d = inline_uint16_prim_num(p, rr);
    uint8_t a = to_any_slot(p, ll);
    if (a > d) { // Free top slot first
        free_expr_slot(p, ll);
        free_expr_slot(p, rr);
    } else {
        free_expr_slot(p, rr);
        free_expr_slot(p, ll);
    }
    int bc = BINOP_BC[op] + (rr->t == EXPR_NUM) + (rr->t == EXPR_PRIM) * 2;
    expr_new(l, EXPR_JMP);
    emit(p, ins2(bc, a, d));
    l->pc = emit_jmp(p);
}

static int fold_rel(int op, Expr *l, Expr r) {
    if (l->t != EXPR_NUM || r.t != EXPR_NUM) {
        return 0;
    }
    double a = l->num, b = r.num;
    int c;
    switch (op) {
        case '<':   c = a < b;  break;
        case TK_LE: c = a <= b; break;
        case '>':   c = a > b;  break;
        case TK_GE: c = a >= b; break;
        default:  UNREACHABLE();
    }
    expr_new(l, EXPR_PRIM);
    l->tag = c ? TAG_TRUE : TAG_FALSE;
    return 1;
}

static void emit_rel(Parser *p, int op, Expr *l, Expr r) {
    if (fold_rel(op, l, r)) {
        return;
    }
    Expr *ll = l, *rr = &r;
    if (ll->t != EXPR_SLOT) {
        ll = &r, rr = l; // Constant on the right
        op = INVERT_TK[op];
    }
    uint16_t d = inline_uint16_num(p, rr);
    uint8_t a = to_any_slot(p, ll);
    if (a > d) { // Free top slot first
        free_expr_slot(p, ll);
        free_expr_slot(p, rr);
    } else {
        free_expr_slot(p, rr);
        free_expr_slot(p, ll);
    }
    int bc = BINOP_BC[op] + (rr->t == EXPR_NUM);
    expr_new(l, EXPR_JMP);
    emit(p, ins2(bc, a, d));
    l->pc = emit_jmp(p);
}

static void emit_and(Parser *p, Expr *l, Expr r) {
    assert(l->true_list == JMP_NONE); // Patched by 'emit_and_left'
    append_jmp(p, &r.false_list, l->false_list);
    *l = r;
}

static void emit_or(Parser *p, Expr *l, Expr r) {
    assert(l->false_list == JMP_NONE); // Patched by 'emit_or_left'
    append_jmp(p, &r.true_list, l->true_list);
    *l = r;
}

static void emit_binop(Parser *p, int op, Expr *l, Expr r) {
    switch (op) {
    case '+': case '-': case '*': case '/': case '%': case '^':
        emit_arith(p, op, l, r);
        break;
    case TK_EQ: case TK_NEQ:
        emit_eq(p, op, l, r);
        break;
    case '<': case TK_LE: case '>': case TK_GE:
        emit_rel(p, op, l, r);
        break;
    case TK_AND: emit_and(p, l, r); break;
    case TK_OR:  emit_or(p, l, r); break;
    default: assert(0); // TODO: remaining binops
    }
}

// Forward declaration
static void parse_subexpr(Parser *p, Expr *l, int min_prec);

static void parse_primary_expr(Parser *p, Expr *l) {
    Token tk;
    ErrInfo info;
    switch (peek_tk(p->l, &tk)) {
    case TK_IDENT:
        find_var(p, l, tk.s);
        read_tk(p->l, NULL);
        break;
    case '(':
        read_tk(p->l, NULL);
        parse_subexpr(p, l, PREC_MIN);
        expect_tk(p->l, ')', NULL);
        break;
    default:
        info = tk2err(&tk);
        err_syntax(p->L, &info, "unexpected symbol");
    }
}

static void parse_operand(Parser *p, Expr *l) {
    Token tk;
    switch (peek_tk(p->l, &tk)) {
    case TK_NIL:
        expr_new(l, EXPR_PRIM);
        l->tag = TAG_NIL;
        break;
    case TK_TRUE:
        expr_new(l, EXPR_PRIM);
        l->tag = TAG_TRUE;
        break;
    case TK_FALSE:
        expr_new(l, EXPR_PRIM);
        l->tag = TAG_FALSE;
        break;
    case TK_NUM:
        expr_new(l, EXPR_NUM);
        l->num = tk.num;
        break;
    default:
        parse_primary_expr(p, l);
        return;
    }
    read_tk(p->l, NULL);
}

static void parse_subexpr(Parser *p, Expr *l, int min_prec) {
    int unop = peek_tk(p->l, NULL);
    if (UNOP_PREC[unop]) {
        read_tk(p->l, NULL); // Skip unop
        parse_subexpr(p, l, UNOP_PREC[unop]);
        emit_unop(p, unop, l);
    } else {
        parse_operand(p, l);
    }

    int binop = peek_tk(p->l, NULL);
    while (BINOP_PREC[binop] > min_prec) {
        read_tk(p->l, NULL); // Skip binop
        emit_binop_left(p, binop, l);
        Expr r;
        parse_subexpr(p, &r, BINOP_PREC[binop] - IS_RASSOC[binop]);
        emit_binop(p, binop, l, r); // 'l' contains the result
        binop = peek_tk(p->l, NULL);
    }
}

static void parse_expr(Parser *p, Expr *e) {
    parse_subexpr(p, e, PREC_MIN);
}


// ---- Statements ----

static void parse_local_def(Parser *p) {
    Token name;
    expect_tk(p->l, TK_IDENT, &name);
    expect_tk(p->l, '=', NULL);
    Expr r;
    parse_expr(p, &r);
    to_next_slot(p, &r);
    def_local(p, name.s);
}

static void parse_local(Parser *p) {
    expect_tk(p->l, TK_LOCAL, NULL);
    if (peek_tk(p->l, NULL) == TK_FUNCTION) {
        assert(0); // TODO
    } else {
        parse_local_def(p);
    }
}

static void parse_stmt(Parser *p) {
    switch (peek_tk(p->l, NULL)) {
        case TK_LOCAL: parse_local(p); break;
        default:       assert(0); // TODO
    }
}

static int is_end_of_block(int tk) {
    return tk == TK_EOF ||
        tk == TK_END ||
        tk == TK_ELSEIF ||
        tk == TK_ELSE ||
        tk == TK_UNTIL;
}

static void parse_block(Parser *p) {
    BlockScope b;
    enter_block(p, &b);
    while (!is_end_of_block(peek_tk(p->l, NULL))) {
        parse_stmt(p);
        if (peek_tk(p->l, NULL) == ';') {
            read_tk(p->l, NULL);
        }
    }
    exit_block(p);
}

void parse(State *L, Reader *r) {
    Lexer l = lexer_new(L, r);
    Parser p = parser_new(L, &l);
    FnScope top_level;
    enter_fn(&p, &top_level);
    parse_block(&p);
    exit_fn(&p);
    assert(!p.f);
    stack_push(L, fn2v(top_level.fn));
}
