
// Auxiliary library implementation
// Adapted from 'lauxlib.c' in the Lua source code

#include "lua.h"
#include "lauxlib.h"

#include <stdlib.h>

static void * alloc_fn(void *ud, void *ptr, size_t osize, size_t nsize) {
    (void) ud; (void) osize; // unused
    if (nsize == 0) {
        free(ptr);
        return NULL;
    } else {
        return realloc(ptr, nsize);
    }
}

LUALIB_API lua_State * luaL_newstate(void) {
    lua_State *L = lua_newstate(alloc_fn, NULL);
    return L;
}

typedef struct { // Userdata for the file reader
    FILE *f;
    char buf[LUAL_BUFFERSIZE];
} FileReader;

static const char * file_reader(lua_State *L, void *ud, size_t *size) {
    (void) L;
    FileReader *r = (FileReader *) ud;
    if (feof(r->f)) {
        return NULL;
    }
    *size = fread(r->buf, 1, sizeof(r->buf), r->f);
    return *size > 0 ? r->buf : NULL;
}

LUALIB_API int luaL_loadfile(lua_State *L, const char *filename) {
    FileReader r;
    if (!filename) {
        r.f = stdin;
        filename = "stdin";
    } else {
        r.f = fopen(filename, "r");
        if (!r.f) {
            return LUA_ERRFILE; // TODO: error message on stack
        }
    }
    int err = lua_load(L, file_reader, &r, filename);
    return err;
}

