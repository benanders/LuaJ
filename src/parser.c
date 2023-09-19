
#include <assert.h>
#include <string.h>
#include <math.h>

#include "parser.h"
#include "lexer.h"
#include "value.h"

// For EXPR_RELOC who haven't had a destination slot assigned yet
#define NO_SLOT 0xff

enum {
    EXPR_PRIM,
    EXPR_NUM,
    EXPR_SLOT,
    EXPR_RELOC,
};

typedef struct {
    int t;
    union {
        uint16_t tag; // EXPR_PRIM
        double num;   // EXPR_NUM
        uint8_t slot; // EXPR_SLOT
        size_t pc;    // EXPR_RELOC
    };
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
    X('+', PREC_ADD, BC_ADDVV, 1, 0) \
    X('-', PREC_ADD, BC_SUBVV, 0, 0) \
    X('*', PREC_MUL, BC_MULVV, 1, 0) \
    X('/', PREC_MUL, BC_DIVVV, 0, 0) \
    X('%', PREC_MUL, BC_MODVV, 0, 0) \
    X('^', PREC_POW, BC_POW,   0, 1)

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

// Forward declaration
static void parse_subexpr(Parser *p, Expr *l, int min_prec);

static inline int is_int(double n, int *i) {
    lua_number2int(*i, n);
    return n == *i;
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
    default: UNREACHABLE();
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

// Inlines a number's index into the function's constants array if it'll fit
// into a 'uint8_t'. Otherwise, puts the expression into a stack slot.
static uint8_t inline_uint8_num(Parser *p, Expr *e) {
    if (e->t == EXPR_NUM && p->f->fn->num_k <= UINT8_MAX) {
        return (uint8_t) emit_k(p, n2v(e->num));
    }
    return to_any_slot(p, e);
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

static void emit_binop_left(Parser *p, int binop, Expr *l) {
    switch (binop) {
    case '+': case '-': case '*': case '/': case '%': case '^':
        if (l->t != EXPR_NUM) {
            to_any_slot(p, l); // Needs to be on the stack
        }
        break;
    default: assert(0); // TODO: remaining binops
    }
}

static int fold_arith(int op, Expr *l, Expr r) {
    if (l->t != EXPR_NUM || r.t != EXPR_NUM) {
        return 0;
    }
    double a = l->num, b = r.num;
    switch (op) {
        case '+': l->num = a + b; break;
        case '-': l->num = a - b; break;
        case '*': l->num = a * b; break;
        case '/': l->num = a / b; break;
        case '%': l->num = fmod(a, b); break;
        case '^': l->num = pow(a, b);  break;
        default:  UNREACHABLE();
    }
    return 1;
}

static void emit_arith(Parser *p, int op, Expr *l, Expr r) {
    if (fold_arith(op, l, r)) {
        return;
    }
    uint8_t b, c;
    if (op == '^') {
        c = to_any_slot(p, &r);
        b = to_any_slot(p, l);
    } else {
        c = inline_uint8_num(p, &r);
        b = inline_uint8_num(p, l);
        assert(l->t == EXPR_SLOT || l->t == EXPR_NUM);
        assert(r.t == EXPR_SLOT || r.t == EXPR_NUM);
    }
    assert(l->t == EXPR_SLOT || r.t == EXPR_SLOT); // Not both consts
    if (b > c) { // Free top slot first
        free_expr_slot(p, l);
        free_expr_slot(p, &r);
    } else {
        free_expr_slot(p, &r);
        free_expr_slot(p, l);
    }
    Expr *ll = l, *rr = &r;
    if (IS_COMMUTATIVE[op] && ll->t == EXPR_NUM) {
        ll = &r, rr = l;
        uint8_t tmp = b; b = c; c = tmp;
    }
    int bc = BINOP_BC[op] + (rr->t == EXPR_NUM) + (ll->t == EXPR_NUM) * 2;
    expr_new(l, EXPR_RELOC);
    l->pc = emit(p, ins3(bc, NO_SLOT, b, c));
}

static void emit_binop(Parser *p, int binop, Expr *l, Expr r) {
    switch (binop) {
    case '+': case '-': case '*': case '/': case '%': case '^':
        emit_arith(p, binop, l, r);
        break;
    default: assert(0); // TODO: remaining binops
    }
}

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
