
#include <assert.h>
#include <string.h>
#include <math.h>

#include "parser.h"
#include "lexer.h"
#include "value.h"

// Used for 'BC_JMP' instructions that have been emitted but haven't had their
// jump target set yet.
#define JMP_NONE (-1)

typedef struct BlockScope {
    struct BlockScope *outer;
    int first_local;
    int is_loop;
    int breaks; // Jump-list for break statements
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

static int emit(Parser *p, BcIns ins, int line) {
    return fn_emit(p->L, p->f->fn, ins, line);
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

static void enter_fn(Parser *p, FnScope *f, Str *name, int start_line) {
    *f = (FnScope) {0};
    f->outer = p->f;
    p->f = f;
    f->fn = fn_new(p->L, name, p->l->r->chunk_name);
    f->fn->start_line = start_line;
}

static void exit_fn(Parser *p, int end_line) {
    assert(p->f);
    p->f->fn->end_line = end_line;
    emit(p, ins0(BC_RET0), end_line);
    p->f = p->f->outer;
}

static void enter_block(Parser *p, BlockScope *b) {
    assert(p->f->num_stack == p->f->num_locals); // No temps on stack
    *b = (BlockScope) {0};
    b->first_local = p->f->num_locals;
    b->outer = p->f->b;
    p->f->b = b;
}

static void enter_loop(Parser *p, BlockScope *b) {
    enter_block(p, b);
    b->is_loop = 1;
    b->breaks = JMP_NONE;
}

static void exit_block(Parser *p) {
    assert(p->f->b);
    p->f->num_locals = p->f->num_stack = p->f->b->first_local;
    p->f->b = p->f->b->outer;
}

static uint8_t reserve_slots(Parser *p, int n) {
    if (p->f->num_stack + n >= UINT8_MAX) { // 254 max (0xff is NO_SLOT)
        Token err;
        peek_tk(p->l, &err);
        ErrInfo info = tk2err(&err);
        err_syntax(p->L, &info, "too many local variables in function");
    }
    uint8_t base = p->f->num_stack;
    p->f->num_stack += n;
    return base;
}

static void def_local(Parser *p, Str *name) {
    p->f->locals[p->f->num_locals++] = name;
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

// For instructions associated with an EXPR_RELOC that haven't had a destination
// slot assigned yet.
#define NO_SLOT 0xff

enum {
    EXPR_PRIM,
    EXPR_NUM,
    EXPR_STR,
    EXPR_LOCAL,
    EXPR_CALL,
    EXPR_NON_RELOC, // An expression result in a fixed stack slot
    EXPR_RELOC,     // An instruction without an assigned stack slot
    EXPR_JMP,       // A condition expression
};

typedef struct {
    int t;
    Token tk; // For errors
    union {
        uint16_t tag; // EXPR_PRIM
        double num;   // EXPR_NUM
        Str *s;       // EXPR_STR
        uint8_t slot; // EXPR_LOCAL, EXPR_NON_RELOC
        int pc;       // EXPR_RELOC, EXPR_JMP, EXPR_CALL
    };
    int true_list, false_list;
} Expr;

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
    X('-',    BC_NEG) \
    X(TK_NOT, BC_NOT)

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
    [BC_IST] = BC_ISF,    [BC_ISTC] = BC_ISFC,
    [BC_ISF] = BC_IST,    [BC_ISFC] = BC_ISTC,
    [BC_EQVV] = BC_NEQVV, [BC_EQVN] = BC_NEQVN,
    [BC_EQVP] = BC_NEQVP, [BC_EQVS] = BC_NEQVS,
    [BC_NEQVV] = BC_EQVV, [BC_NEQVN] = BC_EQVN,
    [BC_NEQVP] = BC_EQVP, [BC_NEQVS] = BC_EQVS,
    [BC_LTVV] = BC_GEVV,  [BC_LTVN] = BC_GEVN,
    [BC_LEVV] = BC_GTVV,  [BC_LEVN] = BC_GTVN,
    [BC_GTVV] = BC_LEVV,  [BC_GTVN] = BC_LEVN,
    [BC_GEVV] = BC_LTVV,  [BC_GEVN] = BC_LTVN,
};

static void expr_new(Expr *e, int t, Token tk) {
    *e = (Expr) {0};
    e->t = t;
    e->tk = tk;
    e->true_list = e->false_list = JMP_NONE;
}

// Is there a jump list associated with the expression value?
static int has_jmp(Expr *e) {
    return e->true_list != JMP_NONE || e->false_list != JMP_NONE;
}

static int is_prim_expr(Expr *e) { return e->t == EXPR_PRIM && !has_jmp(e); }
static int is_num_expr(Expr *e)  { return e->t == EXPR_NUM && !has_jmp(e); }
static int is_str_expr(Expr *e)  { return e->t == EXPR_STR && !has_jmp(e); }
static int is_const_expr(Expr *e) {
    return !has_jmp(e) && (e->t == EXPR_PRIM ||
        e->t == EXPR_NUM ||
        e->t == EXPR_STR);
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

static int emit_jmp(Parser *p) {
    int pc = emit(p, ins0(BC_JMP), -1);
    patch_jmp(p, pc, JMP_NONE);
    return pc;
}

static int follow_jmp(Parser *p, int jmp) {
    assert(jmp != JMP_NONE);
    BcIns *ins = &p->f->fn->ins[jmp];
    int delta = (int) bc_e(*ins);
    return jmp + delta - JMP_BIAS;
}

// Puts the jump list 'to_add' at the start of another jump list 'head'.
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
static int discard_val(Parser *p, int jmp) {
    BcIns *cond = &p->f->fn->ins[jmp > 0 ? jmp - 1 : 0];
    uint8_t op = bc_op(*cond);
    if (op == BC_ISTC || op == BC_ISFC) {
        bc_set_op(cond, op - 1); // ISTC -> IST or ISFC -> ISF
        bc_set_a(cond, 0);
        return 1;
    } else if (bc_a(*cond) == NO_SLOT) { // Relocatable instruction
        *cond = ins0(BC_NOP); // Make the jump unconditional
        return 1;
    } else {
        return 0;
    }
}

// Patches the destination slot for a value associated with a jump (e.g., in
// 'x and 3'). Returns 1 if there was a value to patch/discard.
static int patch_val(Parser *p, int jmp, uint8_t dst) {
    BcIns *cond = &p->f->fn->ins[jmp > 0 ? jmp - 1 : 0];
    uint8_t op = bc_op(*cond);
    if (dst == NO_SLOT) {
        return discard_val(p, jmp);
    } else if (op == BC_ISTC || op == BC_ISFC || bc_a(*cond) == NO_SLOT) {
        bc_set_a(cond, dst);
        return 1;
    } else {
        return 0;
    }
}

// Discards all values associated with jumps along a jump list 'head'.
static void discard_vals(Parser *p, int head) {
    while (head != JMP_NONE) {
        discard_val(p, head);
        head = follow_jmp(p, head);
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
        if (patch_val(p, head, dst)) {
            patch_jmp(p, head, val_target);
        } else {
            patch_jmp(p, head, jmp_target);
        }
        head = next;
    }
}

// Patch all jumps in the jump list to 'target' and discard their values.
static void patch_jmps(Parser *p, int head, int target) {
    patch_jmps_and_vals(p, head, target, NO_SLOT, target);
}

static void patch_jmps_here(Parser *p, int head) {
    // TODO: combine jump chains (jumps to another jump) for efficiency
    int target = p->f->fn->num_ins;
    patch_jmps(p, head, target);
}

// Checks to see if all jumps in the jump list have a value associated with
// them, or if any are pure conditionals.
//
// E.g., the true and false jump lists for 'a and b + 3 or c' all have values
// associated with them. 'a and b == 3 or c' doesn't.
static int jmps_need_fall_through(Parser *p, int head) {
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

static void discharge(Parser *p, Expr *e) {
    switch (e->t) {
    case EXPR_LOCAL:
        e->t = EXPR_NON_RELOC;
        break;
    case EXPR_CALL:
        e->t = EXPR_NON_RELOC;
        e->slot = bc_a(p->f->fn->ins[e->pc]); // Base return slot
        break;
    default: break;
    }
}

static inline int is_int(double n, int *i) {
    lua_number2int(*i, n);
    return n == *i;
}

// Stores the result of an expression into a specific stack slot.
static void to_slot(Parser *p, Expr *e, uint8_t dst) {
    discharge(p, e);
    int i;
    uint16_t idx;
    switch (e->t) {
    case EXPR_PRIM:
        emit(p, ins2(BC_KPRIM, dst, e->tag), e->tk.line);
        break;
    case EXPR_NUM:
        if (is_int(e->num, &i) && i <= UINT16_MAX) {
            emit(p, ins2(BC_KINT, dst, (uint16_t) i), e->tk.line);
        } else {
            idx = emit_k(p, n2v(e->num));
            emit(p, ins2(BC_KNUM, dst, idx), e->tk.line);
        }
        break;
    case EXPR_STR:
        idx = emit_k(p, str2v(e->s));
        emit(p, ins2(BC_KSTR, dst, idx), e->tk.line);
        break;
    case EXPR_NON_RELOC:
        if (dst != e->slot) {
            emit(p, ins2(BC_MOV, dst, e->slot), e->tk.line);
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
        if (jmps_need_fall_through(p, e->true_list) ||
            jmps_need_fall_through(p, e->false_list)) {
            int before = (e->t != EXPR_JMP) ? emit_jmp(p) : JMP_NONE;
            false_case = emit(p, ins2(BC_KPRIM, dst, TAG_FALSE), e->tk.line);
            int middle = emit_jmp(p);
            true_case = emit(p, ins2(BC_KPRIM, dst, TAG_TRUE), e->tk.line);
            patch_jmps_here(p, before);
            patch_jmps_here(p, middle);
        }
        int after = p->f->fn->num_ins;
        patch_jmps_and_vals(p, e->true_list, true_case, dst, after);
        patch_jmps_and_vals(p, e->false_list, false_case, dst, after);
    }
    expr_new(e, EXPR_NON_RELOC, e->tk);
    e->slot = dst;
}

// When calling this function, we know we won't be using 'e's stack slot again.
// If 'e' is at the top of the stack, we can re-use it.
static void free_expr_slot(Parser *p, Expr *e) {
    if (e->t == EXPR_NON_RELOC && e->slot >= p->f->num_locals) {
        p->f->num_stack--;
        assert(e->slot == p->f->num_stack); // Make sure we freed the stack top
    }
}

// Stores the result of an expression into the next available stack slot (e.g.,
// when assigning a local).
static uint8_t to_next_slot(Parser *p, Expr *e) {
    discharge(p, e);
    free_expr_slot(p, e);
    uint8_t dst = reserve_slots(p, 1);
    to_slot(p, e, dst);
    return dst;
}

// Stores the result of an expression into any stack slot. In practice, this
// means expressions already allocated a slot aren't moved, and everything else
// is allocated a new slot.
static uint8_t to_any_slot(Parser *p, Expr *e) {
    discharge(p, e);
    // Local variables with jumps associated need to have 'to_next_slot' called
    if (e->t == EXPR_NON_RELOC && !(has_jmp(e) && e->slot < p->f->num_locals)) {
        to_slot(p, e, e->slot);
        return e->slot;
    }
    return to_next_slot(p, e);
}

static uint8_t inline_uint8_num(Parser *p, Expr *e) {
    if (is_num_expr(e) && p->f->fn->num_k <= UINT8_MAX) {
        return (uint8_t) emit_k(p, n2v(e->num));
    } else {
        return to_any_slot(p, e);
    }
}

static uint16_t inline_uint16_num(Parser *p, Expr *e) {
    if (is_num_expr(e)) {
        int idx = emit_k(p, n2v(e->num));
        assert(idx <= UINT16_MAX);
        return (uint16_t) idx;
    } else {
        return to_any_slot(p, e);
    }
}

static uint16_t inline_uint16_const(Parser *p, Expr *e) {
    if (is_prim_expr(e)) {
        return e->tag;
    } else if (is_str_expr(e)) {
        int idx = emit_k(p, str2v(e->s));
        assert(idx <= UINT16_MAX);
        return (uint16_t) idx;
    } else {
        return inline_uint16_num(p, e);
    }
}

static void invert_cond(Parser *p, int jmp) {
    BcIns *cond = &p->f->fn->ins[jmp - 1];
    int inverted = INVERT_OP[bc_op(*cond)];
    bc_set_op(cond, inverted);
}

static int fold_unop(int op, Expr *l) {
    switch (op) {
    case '-':
        if (!is_num_expr(l)) {
            return 0;
        }
        double v = -l->num;
        expr_new(l, EXPR_NUM, l->tk);
        l->num = v;
        return 1;
    case TK_NOT:
        if (!is_const_expr(l)) {
            return 0;
        }
        int t = !(l->t == EXPR_PRIM && (l->tag == TAG_FALSE || l->tag == TAG_NIL));
        expr_new(l, EXPR_PRIM, l->tk);
        l->tag = t ? TAG_TRUE : TAG_FALSE;
        return 1;
    default: assert(0); // TODO: remaining unops (len)
    }
}

static void emit_unop(Parser *p, Token *op, Expr *l) {
    if (fold_unop(op->t, l)) {
        return;
    }
    if (op->t == TK_NOT) {
        int tmp = l->true_list; // Swap true and false lists
        l->true_list = l->false_list;
        l->false_list = tmp;
        discard_vals(p, l->true_list);
        discard_vals(p, l->false_list);
        discharge(p, l);
        if (l->t == EXPR_JMP) {
            invert_cond(p, l->pc);
            return;
        } // Otherwise, fall through and emit BC_NOT...
    }
    uint8_t d = to_any_slot(p, l); // Must be in a slot
    free_expr_slot(p, l);
    expr_new(l, EXPR_RELOC, *op);
    l->pc = emit(p, ins2(UNOP_BC[op->t], NO_SLOT, d), op->line);
}

// Emits a branch on the "falseness" of 'l' and adds this jump to 'l's false
// jump list. Patches the expression's true jump list to the instruction after
// the emitted branch.
static void emit_branch_true(Parser *p, Expr *l, int line) {
    discharge(p, l);
    int to_add;
    switch (l->t) {
    case EXPR_PRIM:
        if (l->tag == TAG_FALSE || l->tag == TAG_NIL) {
            // 'false and x' always evaluates to false
            to_slot(p, l, NO_SLOT); // Discard values
            to_add = emit_jmp(p); // Unconditional jump to false case
            break;
        } // 'true and x' always evaluates to 'x' -> fall through...
    case EXPR_NUM: // '3 and x' always evaluates to 'x'
    case EXPR_STR:
        to_add = JMP_NONE;
        break;
    case EXPR_JMP: // Branch already emitted
        invert_cond(p, l->pc);
        to_add = l->pc;
        break;
    default:
        emit(p, ins2(BC_ISFC, NO_SLOT, to_any_slot(p, l)), line);
        to_add = emit_jmp(p);
        free_expr_slot(p, l);
        break;
    }
    append_jmp(p, &l->false_list, to_add);
    int next = p->f->fn->num_ins;
    patch_jmps(p, l->true_list, next);
    l->true_list = JMP_NONE;
}

// Emits a branch on the "truthiness" of 'l' and adds this jump to 'l's true
// jump list. Patches the expression's false jump list to the instruction after
// the emitted branch.
static void emit_branch_false(Parser *p, Expr *l, int line) {
    discharge(p, l);
    int to_add;
    switch (l->t) {
    case EXPR_PRIM:
        if (l->tag == TAG_FALSE || l->tag == TAG_NIL) {
            // 'false or x' always evaluates to 'x'
            to_add = JMP_NONE;
        } // 'true and x' always evaluates to 'x' -> fall through...
    case EXPR_NUM: // '3 or x' always evaluates to '3'
    case EXPR_STR:
        to_slot(p, l, NO_SLOT); // Discard values
        to_add = emit_jmp(p);   // Unconditional jump to the true case
        break;
    case EXPR_JMP: // Branch already emitted
        to_add = l->pc;
        break;
    default:
        emit(p, ins2(BC_ISTC, NO_SLOT, to_any_slot(p, l)), line);
        to_add = emit_jmp(p);
        free_expr_slot(p, l);
        break;
    }
    append_jmp(p, &l->true_list, to_add);
    int next = p->f->fn->num_ins;
    patch_jmps(p, l->false_list, next);
    l->false_list = JMP_NONE;
}

static void emit_binop_left(Parser *p, Token *op, Expr *l) {
    switch (op->t) {
    case '+': case '-': case '*': case '/': case '%': case '^':
    case '<': case TK_LE: case '>': case TK_GE:
        if (!is_num_expr(l)) {
            to_any_slot(p, l);
        }
        break;
    case TK_EQ: case TK_NEQ:
        if (!is_const_expr(l)) {
            to_any_slot(p, l);
        }
        break;
    case TK_AND: emit_branch_true(p, l, op->line); break;
    case TK_OR:  emit_branch_false(p, l, op->line); break;
    default: assert(0); // TODO: remaining binops (concat)
    }
}

static int fold_arith(Token *op, Expr *l, Expr r) {
    if (!is_num_expr(l) || !is_num_expr(&r)) {
        return 0;
    }
    double a = l->num, b = r.num, v;
    switch (op->t) {
        case '+': v = a + b; break;
        case '-': v = a - b; break;
        case '*': v = a * b; break;
        case '/': v = a / b; break;
        case '%': v = fmod(a, b); break;
        case '^': v = pow(a, b);  break;
        default:  UNREACHABLE();
    }
    expr_new(l, EXPR_NUM, *op);
    l->num = v;
    return 1;
}

static void emit_arith(Parser *p, Token *op, Expr *l, Expr r) {
    if (fold_arith(op, l, r)) {
        return;
    }
    Expr *ll = l, *rr = &r;
    if (IS_COMMUTATIVE[op->t] && ll->t != EXPR_NON_RELOC) {
        ll = &r, rr = l; // Constant on the right
    }
    uint8_t b, c;
    if (op->t == '^') {
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
    int bc = BINOP_BC[op->t] + (rr->t == EXPR_NUM) + (ll->t == EXPR_NUM) * 2;
    expr_new(l, EXPR_RELOC, *op);
    l->pc = emit(p, ins3(bc, NO_SLOT, b, c), op->line);
}

static int fold_eq(Token *op, Expr *l, Expr r) {
    if (!is_const_expr(l) || !is_const_expr(&r)) {
        return 0;
    }
    int v = 0;
    if (l->t == r.t) {
        switch (l->t) {
            case EXPR_PRIM: v = (op->t == TK_NEQ) ^ (l->tag == r.tag); break;
            case EXPR_NUM:  v = (op->t == TK_NEQ) ^ (l->num == r.num); break;
            case EXPR_STR:  v = (op->t == TK_NEQ) ^ (str_eq(l->s, r.s)); break;
        }
    }
    expr_new(l, EXPR_PRIM, *op);
    l->tag = v ? TAG_TRUE : TAG_FALSE;
    return 1;
}

static void emit_eq(Parser *p, Token *op, Expr *l, Expr r) {
    if (fold_eq(op, l, r)) {
        return;
    }
    Expr *ll = l, *rr = &r;
    if (ll->t != EXPR_NON_RELOC) {
        ll = &r, rr = l; // Constant on the right
    }
    uint16_t d = inline_uint16_const(p, rr);
    uint8_t a = to_any_slot(p, ll);
    if (a > d) { // Free top slot first
        free_expr_slot(p, ll);
        free_expr_slot(p, rr);
    } else {
        free_expr_slot(p, rr);
        free_expr_slot(p, ll);
    }
    int bc = BINOP_BC[op->t];
    switch (rr->t) {
        case EXPR_PRIM: bc += 1; break; // EQVV -> EQVP or NEQVV -> NEQVP
        case EXPR_NUM:  bc += 2; break; // EQVV -> EQVN or NEQVV -> NEQVN
        case EXPR_STR:  bc += 3; break; // EQVV -> EQVS or NEQVV -> NEQVS
    }
    expr_new(l, EXPR_JMP, *op);
    emit(p, ins2(bc, a, d), op->line);
    l->pc = emit_jmp(p);
}

static int fold_rel(Token *op, Expr *l, Expr r) {
    if (!is_num_expr(l) || !is_num_expr(&r)) {
        return 0;
    }
    double a = l->num, b = r.num;
    int v;
    switch (op->t) {
        case '<':   v = a < b;  break;
        case TK_LE: v = a <= b; break;
        case '>':   v = a > b;  break;
        case TK_GE: v = a >= b; break;
        default:  UNREACHABLE();
    }
    expr_new(l, EXPR_PRIM, *op);
    l->tag = v ? TAG_TRUE : TAG_FALSE;
    return 1;
}

static void emit_rel(Parser *p, Token *op, Expr *l, Expr r) {
    if (fold_rel(op, l, r)) {
        return;
    }
    int op_t = op->t;
    Expr *ll = l, *rr = &r;
    if (ll->t != EXPR_NON_RELOC) {
        ll = &r, rr = l; // Constant on the right
        op_t = INVERT_TK[op_t];
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
    int bc = BINOP_BC[op_t] + (rr->t == EXPR_NUM);
    expr_new(l, EXPR_JMP, *op);
    emit(p, ins2(bc, a, d), op->line);
    l->pc = emit_jmp(p);
}

static void emit_and(Parser *p, Expr *l, Expr r) {
    assert(l->true_list == JMP_NONE); // Patched by 'emit_and_left'
    discharge(p, &r);
    append_jmp(p, &r.false_list, l->false_list);
    *l = r;
}

static void emit_or(Parser *p, Expr *l, Expr r) {
    assert(l->false_list == JMP_NONE); // Patched by 'emit_or_left'
    discharge(p, &r);
    append_jmp(p, &r.true_list, l->true_list);
    *l = r;
}

static void emit_binop(Parser *p, Token *op, Expr *l, Expr r) {
    switch (op->t) {
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
    default: assert(0); // TODO: remaining binops (concat)
    }
}

static void find_var(Parser *p, Expr *e, Token *name) {
    for (int i = p->f->num_locals - 1; i >= 0; i--) { // In reverse
        Str *l = p->f->locals[i];
        if (name->s->len == l->len &&
                strncmp(str_val(name->s), str_val(l), l->len) == 0) {
            expr_new(e, EXPR_LOCAL, *name);
            e->slot = i;
            return;
        }
    }
    assert(0); // TODO: upvalues and globals
}

// Forward declarations
static void parse_block(Parser *p);
static int parse_expr_list(Parser *p, Expr *e);
static void parse_subexpr(Parser *p, Expr *l, int min_prec);

static int parse_params(Parser *p) {
    expect_tk(p->l, '(', NULL);
    int num_params = 0;
    Token name;
    while (peek_tk(p->l, &name) == TK_IDENT) {
        read_tk(p->l, NULL);
        def_local(p, name.s);
        reserve_slots(p, 1);
        if (peek_tk(p->l, NULL) == ',') {
            read_tk(p->l, NULL);
        } else {
            break;
        }
    }
    expect_tk(p->l, ')', NULL);
    return num_params;
}

static void parse_fn_body(Parser *p, Expr *l, Token *fn_tk, Str *fn_name) {
    FnScope f;
    enter_fn(p, &f, fn_name, fn_tk->line);
    f.fn->num_params = parse_params(p);
    parse_block(p);
    Token end_tk;
    expect_tk(p->l, TK_END, &end_tk);
    exit_fn(p, end_tk.line);
    uint16_t idx = emit_k(p, fn2v(f.fn));
    expr_new(l, EXPR_RELOC, *fn_tk);
    l->pc = emit(p, ins2(BC_KFN, NO_SLOT, idx), fn_tk->line);
}

static void parse_primary_expr(Parser *p, Expr *l) {
    Token tk;
    ErrInfo info;
    switch (peek_tk(p->l, &tk)) {
    case TK_IDENT:
        find_var(p, l, &tk);
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

static void parse_call_expr(Parser *p, Expr *l) {
    uint8_t base = to_next_slot(p, l);
    Token call;
    expect_tk(p->l, '(', &call);
    int num_args = 0;
    if (peek_tk(p->l, NULL) != ')') {
        num_args = parse_expr_list(p, l);
        to_next_slot(p, l); // Contiguous slots for arguments
    }
    expect_tk(p->l, ')', NULL);
    // By default, 'BC_CALL' returns a single value unless modified by the
    // calling context (e.g., in parse_call_or_assign or parse_assign)
    expr_new(l, EXPR_CALL, call);
    l->pc = emit(p, ins3(BC_CALL, base, num_args, 1), call.line);
    p->f->num_stack = base + 1;
}

static int parse_suffix(Parser *p, Expr *l) {
    switch (peek_tk(p->l, NULL)) {
    case '(': // Function call
        parse_call_expr(p, l);
        return 1;
    }
    return 0;
}

static void parse_suffixes(Parser *p, Expr *l) {
    while (parse_suffix(p, l));
}

static void parse_suffixed_expr(Parser *p, Expr *l) {
    parse_primary_expr(p, l);
    parse_suffixes(p, l);
}

static void parse_operand(Parser *p, Expr *l) {
    Token tk;
    switch (peek_tk(p->l, &tk)) {
    case TK_NIL:
        expr_new(l, EXPR_PRIM, tk);
        l->tag = TAG_NIL;
        break;
    case TK_TRUE:
        expr_new(l, EXPR_PRIM, tk);
        l->tag = TAG_TRUE;
        break;
    case TK_FALSE:
        expr_new(l, EXPR_PRIM, tk);
        l->tag = TAG_FALSE;
        break;
    case TK_NUM:
        expr_new(l, EXPR_NUM, tk);
        l->num = tk.num;
        break;
    case TK_STR:
        expr_new(l, EXPR_STR, tk);
        l->s = tk.s;
        break;
    case TK_FUNCTION:
        read_tk(p->l, NULL); // Skip 'function'
        parse_fn_body(p, l, &tk, NULL);
        return;
    default:
        parse_suffixed_expr(p, l);
        return;
    }
    read_tk(p->l, NULL);
}

static void parse_subexpr(Parser *p, Expr *l, int min_prec) {
    Token unop;
    peek_tk(p->l, &unop);
    if (UNOP_PREC[unop.t]) {
        read_tk(p->l, NULL); // Skip unop
        parse_subexpr(p, l, UNOP_PREC[unop.t]);
        emit_unop(p, &unop, l);
    } else {
        parse_operand(p, l);
    }
    Token binop;
    peek_tk(p->l, &binop);
    while (BINOP_PREC[binop.t] > min_prec) {
        read_tk(p->l, NULL); // Skip binop
        emit_binop_left(p, &binop, l);
        Expr r;
        parse_subexpr(p, &r, BINOP_PREC[binop.t] - IS_RASSOC[binop.t]);
        emit_binop(p, &binop, l, r); // 'l' contains the result
        peek_tk(p->l, &binop);
    }
}

static void parse_expr(Parser *p, Expr *e) {
    parse_subexpr(p, e, PREC_MIN);
}

static int parse_expr_list(Parser *p, Expr *e) {
    int n = 1;
    parse_expr(p, e);
    while (peek_tk(p->l, NULL) == ',') {
        read_tk(p->l, NULL);
        to_next_slot(p, e);
        parse_expr(p, e);
        n++;
    }
    return n;
}

// Patches the condition's true jump list to the instruction immediately after
// the condition. Returns the condition's false jump list that needs to be
// patched.
static int parse_cond_expr(Parser *p) {
    Expr cond;
    parse_expr(p, &cond);
    if (cond.t == EXPR_PRIM && cond.tag == TAG_NIL) {
        cond.tag = TAG_FALSE;
    }
    emit_branch_true(p, &cond, cond.tk.line);
    return cond.false_list;
}


// ---- Statements ----

static void parse_local_fn(Parser *p) {
    Token fn_tk;
    expect_tk(p->l, TK_FUNCTION, &fn_tk);
    Token name;
    expect_tk(p->l, TK_IDENT, &name);
    def_local(p, name.s); // Def before body to allow recursion
    Expr l;
    parse_fn_body(p, &l, &fn_tk, name.s);
    to_next_slot(p, &l);
}

static void emit_knil(Parser *p, uint8_t base, int n, int line) {
    // TODO: combine with previous knil/kprim instructions
    if (n == 1) {
        emit(p, ins2(BC_KPRIM, base, TAG_NIL), line);
    } else {
        emit(p, ins2(BC_KNIL, base, base + n - 1), line);
    }
    reserve_slots(p, n);
}

static void adjust_assign(
        Parser *p,
        int num_vars,
        int num_exprs,
        Expr *r,
        int line) {
    int extra = num_vars - num_exprs;
    if (r->t == EXPR_CALL) {
        int num_rets = extra < 0 ? 0 : extra + 1;
        bc_set_c(&p->f->fn->ins[r->pc], num_rets);
    } else {
        to_next_slot(p, r); // Contiguous expression slots
        if (extra > 0) { // Set extra vars to nil
            emit_knil(p, p->f->num_locals - extra, extra, line);
        }
    }
}

static int parse_local_lhs(Parser *p, Str **names) {
    int num_vars = 0;
    Token name;
    while (peek_tk(p->l, &name) == TK_IDENT) {
        read_tk(p->l, NULL);
        if (num_vars >= LUAI_MAXVARS) {
            ErrInfo info = tk2err(&name);
            err_syntax(p->L, &info, "too many local variables in function");
        }
        names[num_vars++] = name.s;
        if (peek_tk(p->l, NULL) != ',') {
            break;
        } else {
            read_tk(p->l, NULL);
        }
    }
    return num_vars;
}

static void parse_local_var(Parser *p) {
    Str *names[LUAI_MAXVARS];
    int num_vars = parse_local_lhs(p, names);
    Token assign;
    expect_tk(p->l, '=', &assign);
    Expr r;
    int num_exprs = parse_expr_list(p, &r);
    for (int i = 0; i < num_vars; i++) {
        def_local(p, names[i]);
    }
    adjust_assign(p, num_vars, num_exprs, &r, assign.line);
    p->f->num_stack = p->f->num_locals; // Drop extra expressions
}

static void parse_local(Parser *p) {
    expect_tk(p->l, TK_LOCAL, NULL);
    if (peek_tk(p->l, NULL) == TK_FUNCTION) {
        parse_local_fn(p);
    } else {
        parse_local_var(p);
    }
}

static int parse_assign_lhs(Parser *p, Expr *l, Expr *vars) {
    if (l->t != EXPR_LOCAL) {
        ErrInfo info = tk2err(&l->tk);
        err_syntax(p->L, &info, "unexpected symbol");
    }
    int num_vars = 0;
    vars[num_vars++] = *l;
    while (peek_tk(p->l, NULL) == ',') {
        read_tk(p->l, NULL); // Skip ','
        if (num_vars >= LUAI_MAXVARS) {
            Token var;
            peek_tk(p->l, &var);
            ErrInfo info = tk2err(&var);
            err_syntax(p->L, &info, "too many variables in assignment");
        }
        parse_suffixed_expr(p, l);
        if (l->t != EXPR_LOCAL) {
            ErrInfo info = tk2err(&l->tk);
            err_syntax(p->L, &info, "expected variable in assignment");
        }
        vars[num_vars++] = *l;
    }
    return num_vars;
}

static void parse_assign(Parser *p, Expr *l) {
    Expr vars[LUAI_MAXVARS];
    int num_vars = parse_assign_lhs(p, l, vars);
    Token assign;
    expect_tk(p->l, '=', &assign);
    Expr r;
    int num_exprs = parse_expr_list(p, &r);
    if (num_vars == num_exprs) {
        // Put last expression directly into the last variable
        Expr *last_var = &vars[num_vars - 1];
        discharge(p, &r);
        free_expr_slot(p, &r);
        to_slot(p, &r, last_var->slot);
        num_vars--;
    } else {
        adjust_assign(p, num_vars, num_exprs, &r, assign.line);
    }
    for (int i = num_vars - 1; i >= 0; i--) {
        uint8_t expr_slot = p->f->num_stack - num_exprs + i;
        emit(p, ins2(BC_MOV, vars[i].slot, expr_slot), assign.line);
    }
    p->f->num_stack = p->f->num_locals; // Drop expressions
}

static void parse_call(Parser *p, Expr *l) {
    parse_suffixes(p, l);
    if (l->t != EXPR_CALL) {
        ErrInfo info = tk2err(&l->tk);
        err_syntax(p->L, &info, "expected assignment or function call");
    }
    bc_set_c(&p->f->fn->ins[l->pc], 0); // No return value
    p->f->num_stack--; // Pop return value off stack
}

static void parse_assign_or_call(Parser *p) {
    Expr l;
    parse_primary_expr(p, &l);
    if (peek_tk(p->l, NULL) == ',' || peek_tk(p->l, NULL) == '=') {
        parse_assign(p, &l);
    } else {
        parse_call(p, &l);
    }
}

static void parse_do(Parser *p) {
    expect_tk(p->l, TK_DO, NULL);
    parse_block(p);
    expect_tk(p->l, TK_END, NULL);
}

static int parse_then(Parser *p) {
    int false_list = parse_cond_expr(p);
    expect_tk(p->l, TK_THEN, NULL);
    parse_block(p);
    return false_list; // False jump list that still needs patching
}

static void parse_if(Parser *p) {
    expect_tk(p->l, TK_IF, NULL);
    int end_jmps = JMP_NONE;
    int false_jmps = parse_then(p);
    while (peek_tk(p->l, NULL) == TK_ELSEIF) {
        read_tk(p->l, NULL);
        append_jmp(p, &end_jmps, emit_jmp(p));
        patch_jmps_here(p, false_jmps);
        false_jmps = parse_then(p);
    }
    if (peek_tk(p->l, NULL) == TK_ELSE) {
        read_tk(p->l, NULL);
        append_jmp(p, &end_jmps, emit_jmp(p));
        patch_jmps_here(p, false_jmps);
        parse_block(p);
    } else {
        append_jmp(p, &end_jmps, false_jmps);
    }
    expect_tk(p->l, TK_END, NULL);
    patch_jmps_here(p, end_jmps);
}

static void parse_while(Parser *p) {
    expect_tk(p->l, TK_WHILE, NULL);
    int start = p->f->fn->num_ins;
    BlockScope loop;
    enter_loop(p, &loop);
    int cond_false_list = parse_cond_expr(p);
    expect_tk(p->l, TK_DO, NULL);
    parse_block(p);
    expect_tk(p->l, TK_END, NULL);
    int end_jmp = emit_jmp(p);
    patch_jmps(p, end_jmp, start);
    patch_jmps_here(p, cond_false_list);
    exit_block(p);
    patch_jmps_here(p, loop.breaks);
}

static void parse_repeat(Parser *p) {
    expect_tk(p->l, TK_REPEAT, NULL);
    int start = p->f->fn->num_ins;
    BlockScope loop;
    enter_loop(p, &loop);
    parse_block(p);
    expect_tk(p->l, TK_UNTIL, NULL);
    int cond_false_list = parse_cond_expr(p);
    patch_jmps(p, cond_false_list, start); // jump back if !cond
    exit_block(p);
    patch_jmps_here(p, loop.breaks);
}

static void parse_break(Parser *p) {
    Token tk;
    expect_tk(p->l, TK_BREAK, &tk);
    BlockScope *loop = p->f->b;
    while (loop && !loop->is_loop) {
        loop = loop->outer;
    }
    if (!loop) {
        ErrInfo info = tk2err(&tk);
        err_syntax(p->L, &info, "no loop to break");
    }
    append_jmp(p, &loop->breaks, emit_jmp(p));
}

static int is_end_of_block(int tk); // Forward declaration

static void parse_return(Parser *p) {
    Token ret;
    expect_tk(p->l, TK_RETURN, &ret);
    int next = peek_tk(p->l, NULL);
    if (is_end_of_block(next) || next == ';') { // No return values
        emit(p, ins0(BC_RET0), ret.line);
    } else { // One or more return values
        Expr e;
        int num_ret = parse_expr_list(p, &e);
        if (num_ret == 1) {
            uint8_t slot = to_any_slot(p, &e);
            emit(p, ins1(BC_RET1, slot), ret.line);
        } else {
            to_next_slot(p, &e); // Force contiguous slots
            emit(p, ins2(BC_RET, p->f->num_locals, num_ret), ret.line);
        }
        p->f->num_stack -= num_ret; // Clean up the stack
    }
}

static void parse_stmt(Parser *p) {
    switch (peek_tk(p->l, NULL)) {
        case TK_FUNCTION: assert(0); // TODO
        case TK_LOCAL:    parse_local(p); break;
        case TK_DO:       parse_do(p); break;
        case TK_IF:       parse_if(p); break;
        case TK_WHILE:    parse_while(p); break;
        case TK_REPEAT:   parse_repeat(p); break;
        case TK_FOR:      assert(0); // TODO
        case TK_BREAK:    parse_break(p); break;
        case TK_RETURN:   parse_return(p); break;
        default:          parse_assign_or_call(p); break;
    }
    // Make sure each statement cleans up after itself
    assert(p->f->num_stack == p->f->num_locals);
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
    Token first_tk;
    peek_tk(&l, &first_tk);
    FnScope top_level;
    enter_fn(&p, &top_level, NULL, first_tk.line);
    parse_block(&p);
    Token last_tk;
    peek_tk(&l, &last_tk);
    exit_fn(&p, last_tk.line);
    assert(!p.f);
    stack_push(L, fn2v(top_level.fn));
}
