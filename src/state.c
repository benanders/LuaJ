
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include "state.h"
#include "reader.h"
#include "lexer.h"
#include "parser.h"
#include "value.h"
#include "debug.h"

LUA_API lua_State * lua_newstate(lua_Alloc f, void *ud) {
    State *L = f(ud, NULL, 0, sizeof(State));
    if (!L) {
        return NULL;
    }
    L->alloc_fn = f;
    L->alloc_ud = ud;
    L->err = NULL;
    L->stack_size = 4096;
    L->stack = L->top = mem_alloc(L, L->stack_size * sizeof(uint64_t));
    return L;
}

LUA_API void lua_close(lua_State *L) {
    mem_free(L, L->stack, L->stack_size * sizeof(uint64_t));
    mem_free(L, L, sizeof(State));
}

static void load_protected(State *L, void *ud) {
    Reader *r = (Reader *) ud;
    parse(L, r);
    uint64_t v = stack_pop(L);
    assert(is_fn(v));
    Fn *f = v2fn(v);
    print_fn(L, f);
}

LUA_API int lua_load(
        lua_State *L,
        lua_Reader reader,
        void *data,
        const char *src_name) {
    Reader r = reader_new(L, reader, data, src_name);
    int err_code = pcall(L, load_protected, &r);
    if (err_code) {
        uint64_t err = stack_pop(L);
        if (!is_nil(err)) {
            assert(is_str(err));
            Str *msg = v2str(err);
            fprintf(stderr, "%.*s\n", (int) msg->len, str_val(msg));
        }
    }
    return err_code;
}

void stack_push(State *L, uint64_t v) {
    ptrdiff_t n = L->top - L->stack;
    if (n >= L->stack_size) {
        L->stack = mem_realloc(
                L,
                L->stack,
                L->stack_size * sizeof(uint64_t),
                L->stack_size * sizeof(uint64_t) * 2);
        L->stack_size *= 2;
        L->top = &L->stack[n];
    }
    *(L->top++) = v;
}

uint64_t stack_pop(State *L) {
    assert(L->top > L->stack);
    return *(--L->top);
}


// ---- Error Handling ----

int pcall(State *L, ProtectedFn f, void *ud) {
    ptrdiff_t saved_top = L->top - L->stack; // Save state
    ErrCtx j = {0};
    j.parent = L->err;
    L->err = &j;
    LUAI_TRY(L, &j,
         f(L, ud);
    )
    L->err = j.parent; // Restore previous error recovery point
    uint64_t err = VAL_NIL;
    if (j.err_code) {
        err = stack_pop(L);
    }
    L->top = L->stack + saved_top; // Restore stack
    if (j.err_code) {
        stack_push(L, err);
    }
    return j.err_code;
}

__attribute__((noreturn))
static void trigger(State *L, int err_code) {
    if (L->err) { // If there's an error recovery point
        L->err->err_code = err_code;
        LUAI_THROW(L, L->err);
    } else {
        exit(EXIT_FAILURE);
    }
}

void err_syntax(State *L, void *tk_ptr, char *fmt, ...) {
    Token *tk = (Token *) tk_ptr;
    char *src_name = tk->src_name;
    if (!src_name) {
        src_name = "<unknown>";
    }
    int prefix_len = snprintf(NULL, 0, "%s:%d:%d: ", src_name, tk->line, tk->col);
    char *prefix = mem_alloc(L, sizeof(char) * (prefix_len + 1));
    sprintf(prefix, "%s:%d:%d: ", src_name, tk->line, tk->col);

    va_list args1, args2;
    va_start(args1, fmt);
    va_copy(args2, args1);
    int msg_len = vsnprintf(NULL, 0, fmt, args1);
    int len = prefix_len + msg_len;
    char *msg = mem_alloc(L, sizeof(char) * (len + 1));
    strncpy(msg, prefix, prefix_len);
    vsprintf(&msg[prefix_len], fmt, args2);
    va_end(args1);
    va_end(args2);
    Str *str = str_new(L, msg, len);
    stack_push(L, str2v(str));
    trigger(L, LUA_ERRSYNTAX);
}

void err_mem(State *L) {
    trigger(L, LUA_ERRMEM);
}


// ---- Memory Allocation ----

static void * realloc_raw(
        State *L,
        void *ptr,
        size_t old_size,
        size_t new_size) {
    assert((old_size == 0) == (ptr == NULL)); // ptr = NULL <=> bytes = 0
    ptr = L->alloc_fn(L->alloc_ud, ptr, old_size, new_size);
    if (new_size > 0 && !ptr) {
        err_mem(L);
    }
    assert((new_size == 0) == (ptr == NULL)); // ptr = NULL <=> bytes = 0
    return ptr;
}

void * mem_alloc(State *L, size_t bytes) {
    return realloc_raw(L, NULL, 0, bytes);
}

void * mem_realloc(State *L, void *ptr, size_t old_bytes, size_t new_bytes) {
    return realloc_raw(L, ptr, old_bytes, new_bytes);
}

void mem_free(State *L, void *ptr, size_t bytes) {
    realloc_raw(L, ptr, bytes, 0);
}
