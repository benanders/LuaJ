
#ifndef LUAJ_STATE_H
#define LUAJ_STATE_H

// The LuaJ state contains everything needed to parse and run Lua programs.
// It corresponds to the 'lua_State' struct in the Lua API.
//
// This file also implements all the library methods from 'lua.h'.

#include <lua.h>

#include <stdint.h>
#include <setjmp.h>

// Handles protected calls and error catching during parsing and runtime.
typedef struct ErrCtx {
    struct ErrCtx *parent; // Linked list for nested pcalls
    luai_jmpbuf b;
    volatile int err_code;
} ErrCtx;

typedef void (*ProtectedFn)(struct lua_State *L, void *ud);

typedef struct lua_State {
    // Memory allocation
    lua_Alloc alloc_fn;
    void *alloc_ud;

    // Error handling
    ErrCtx *err;

    // Stack
    uint64_t *stack;
    uint64_t *top;
    int cap;
} State;

void stack_push(State *L, uint64_t v);
uint64_t stack_pop(State *L);

int pcall(State *L, ProtectedFn f, void *ud);
void err_syntax(State *L, void *tk, char *fmt, ...) __attribute__((noreturn));
void err_mem(State *L) __attribute__((noreturn));

void * mem_alloc(State *L, size_t bytes);
void * mem_realloc(State *L, void *ptr, size_t old_bytes, size_t new_bytes);
void mem_free(State *L, void *ptr, size_t bytes);

#endif
