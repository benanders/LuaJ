
#include "parser.h"
#include "lexer.h"
#include "value.h"

void parse(State *L, Reader *r) {
    Lexer l = lexer_new(L, r);
    expect_tk(&l, TK_LOCAL, NULL);
    Fn *f = fn_new(L);
    stack_push(L, fn2v(f));
}
