
#include <assert.h>

#include "parser.h"
#include "lexer.h"
#include "value.h"

// Info for each block.
typedef struct BlockScope {
    struct BlockScope *outer;
    int first_local;
} BlockScope;

// Info for each function. Keeps track of the innermost block currently being
// parsed.
typedef struct FnScope {
    struct FnScope *outer;
    Fn *fn;
    int num_stack, num_locals;
    Str *locals[LUAI_MAXVARS];
    BlockScope *b;
} FnScope;

// Info for each source code chunk being parsed. Keeps track of the current
// function being parsed.
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

// Emits a bytecode instruction to the function that's currently being parsed.
static int emit(Parser *p, BcIns ins) {
    return fn_emit(p->L, p->f->fn, ins);
}

// Emits a constant value for the function that's currently being parsed.
static int emit_k(Parser *p, uint64_t k) {
    int idx = fn_emit_k(p->L, p->f->fn, k);
    if (idx > UINT16_MAX) {
        Token err;
        peek_tk(p->l, &err);
        err_syntax(p->L, &err, "too many constants in function");
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
        err_syntax(p->L, &err, "too many local variables in function");
    }
    return p->f->num_stack++;
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

enum {
    EXPR_KNUM,
    EXPR_SLOT,
    EXPR_RELOC,
};

typedef struct {
    int t;
    union {
        double num;   // EXPR_KNUM
        uint8_t slot; // EXPR_SLOT
    };
} Expr;

static void expr_new(Expr *e, int t) {
    *e = (Expr) {0};
    e->t = t;
}

// When calling this function, we know we won't be using 'e's stack slot again.
// If 'e' is at the top of the stack, we can re-use it.
static void free_expr_slot(Parser *p, Expr *e) {
    if (e->t == EXPR_SLOT && e->slot >= p->f->num_locals) {
        p->f->num_stack--;
        assert(e->slot == p->f->num_stack); // Make sure we freed the stack top
    }
}

static inline int is_int(double n, int *i) {
    lua_number2int(*i, n);
    return n == *i;
}

// Stores the result of an expression into a specific stack slot.
static void to_slot(Parser *p, Expr *e, uint8_t dst) {
    int i;
    switch (e->t) {
    case EXPR_KNUM:
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
    }
    expr_new(e, EXPR_SLOT);
    e->slot = dst;
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

static void parse_expr(Parser *p, Expr *e) {
    Token num;
    expect_tk(p->l, TK_NUM, &num);
    e->t = EXPR_KNUM;
    e->num = num.num;
}


// ---- Statements ----

static void parse_local_def(Parser *p) {
    Token name;
    expect_tk(p->l, TK_IDENT, &name);
    expect_tk(p->l, '=', NULL);
    Expr r;
    parse_expr(p, &r);
    to_next_slot(p, &r);
}

static void parse_local(Parser *p) {
    expect_tk(p->l, TK_LOCAL, NULL);
    if (peek_tk(p->l, NULL) == TK_FUNCTION) {
        // TODO
        assert(0);
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
