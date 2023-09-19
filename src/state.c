
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include "state.h"
#include "reader.h"
#include "lexer.h"
#include "parser.h"
#include "value.h"
#include "vm.h"

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

static void print_err(State *L, int status) {
    if (status) { // TODO: move to CLI once stack manipulation API exposed
        uint64_t err = stack_pop(L);
        if (!is_nil(err)) {
            assert(is_str(err));
            Str *msg = v2str(err);
            fprintf(stderr, "%.*s\n", (int) msg->len, str_val(msg));
        }
    }
}

static void load_protected(State *L, void *ud) {
    parse(L, (Reader *) ud);
}

// Loads a Lua chunk without running it. If there are no errors, 'lua_load '
// pushes the compiled chunk as a Lua function on top of the stack. Otherwise,
// it pushes an error message.
//
// 'chunk_name' is used in error and debug messages.
LUA_API int lua_load(
        State *L,
        lua_Reader reader_fn,
        void *data,
        const char *chunk_name) {
    Reader r = reader_new(L, reader_fn, data, (char *) chunk_name);
    int status = pcall(L, load_protected, &r);
    print_err(L, status);
    return status;
}

// The following protocol for function calls is used (from the Lua C API):
//
// First, the function to be called is pushed onto the stack; then, the
// arguments to the call are pushed in direct order; that is, the first
// argument is pushed first. Finally, you call 'lua_call'; nargs is the number
// of arguments that you pushed onto the stack. When the function returns, all
// arguments and the function value are popped and the call results are pushed
// onto the stack. The number of results is adjusted to num_results, unless
// num_results is LUA_MULTRET. In this case, all results from the function are
// pushed. The function results are pushed onto the stack in direct order (the
// first result is pushed first), so that after the call the last result is on
// the top of the stack.
LUA_API void (lua_call) (State *L, int num_args, int num_results) {
    // Ignores 'num_args' and 'num_results' now until function calls are
    // implemented; just expects a function prototype to be on the stack
    execute(L);
}

static void call_protected(State *L, void *ud) {
    (void) ud;
    execute(L);
}

// If there are no errors during the call, 'lua_pcall' behaves exactly like
// 'lua_call'. However, if there is any error, 'lua_pcall' catches it, pushes a
// single value on the stack (the error object), and returns an error code. Like
// 'lua_call', 'lua_pcall' always removes the function and its arguments from
// the stack.
//
// If 'err_handler_fn' is 0, then the error object returned on the stack is
// exactly the original error object. Otherwise, 'err_handler_fn' is the stack
// index of a message handler. In case of runtime errors, this handler will be
// called with the error object and its return value will be the object
// returned on the stack by 'lua_pcall'.
//
// Typically, the message handler is used to add more debug information to the
// error object, such as a stack traceback. Such information cannot be gathered
// after the return of 'lua_pcall', since by then the stack has unwound.
LUA_API int (lua_pcall) (
        State *L,
        int num_args,
        int num_results,
        int err_handler_fn) {
    // Ignores 'num_args' and 'num_results' now until function calls are
    // implemented; just expects a function prototype to be on the stack
    int status = pcall(L, call_protected, NULL);
    print_err(L, status);
    return status;
}


// ---- Stack Manipulation ----

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
    ptrdiff_t saved_top = L->top - L->stack; // Save stack
    Err err = {0};
    err.parent = L->err;
    L->err = &err;
    LUAI_TRY(L, &err,
         f(L, ud);
    )
    L->err = err.parent; // Restore previous error recovery point
    if (err.status) {
        uint64_t err_msg = stack_pop(L);
        L->top = L->stack + saved_top; // Restore stack
        stack_push(L, err_msg);
    }
    return err.status;
}

__attribute__((noreturn))
static void trigger(State *L, int status) {
    if (L->err) { // If there's an error recovery point
        L->err->status = status;
        LUAI_THROW(L, L->err);
    } else {
        exit(status);
    }
}

static char * err_prefix(State *L, ErrInfo *info, size_t *prefix_len) {
    char *name = "<unknown>: ";
    if (!info) {
        *prefix_len = strlen(name);
        return name;
    }
    if (info->chunk_name) {
        name = info->chunk_name;
    }
    char *prefix;
    if (info->line >= 1 && info->col >= 1) {
        *prefix_len = snprintf(NULL, 0, "%s:%d:%d: ", name, info->line, info->col);
        prefix = mem_alloc(L, sizeof(char) * (*prefix_len + 1));
        sprintf(prefix, "%s:%d:%d: ", name, info->line, info->col);
    } else if (info->line >= 1) {
        *prefix_len = snprintf(NULL, 0, "%s:%d: ", name, info->line);
        prefix = mem_alloc(L, sizeof(char) * (*prefix_len + 1));
        sprintf(prefix, "%s:%d: ", name, info->line);
    } else {
        *prefix_len = snprintf(NULL, 0, "%s: ", name);
        prefix = mem_alloc(L, sizeof(char) * (*prefix_len + 1));
        sprintf(prefix, "%s: ", name);
    }
    return prefix;
}

static void err_msg(State *L, ErrInfo *info, char *fmt, va_list args) {
    size_t prefix_len;
    char *prefix = err_prefix(L, info, &prefix_len);
    va_list args2;
    va_copy(args2, args);
    size_t msg_len = vsnprintf(NULL, 0, fmt, args);
    size_t len = prefix_len + msg_len;
    char *msg = mem_alloc(L, sizeof(char) * (len + 1));
    strncpy(msg, prefix, prefix_len);
    vsprintf(&msg[prefix_len], fmt, args2);
    va_end(args2);
    Str *str = str_new(L, msg, len);
    stack_push(L, str2v(str));
}

void err_syntax(State *L, ErrInfo *info, char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    err_msg(L, info, fmt, args);
    va_end(args);
    trigger(L, LUA_ERRSYNTAX);
}

void err_run(State *L, ErrInfo *info, char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    err_msg(L, info, fmt, args);
    va_end(args);
    trigger(L, LUA_ERRRUN);
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
