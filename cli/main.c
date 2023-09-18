
// LuaJ command line interpreter
// Uses the Lua C API only

#include <stdlib.h>

#include <lua.h>
#include <lauxlib.h>

static void write(char *prog_name, char *msg) {
    if (prog_name) {
        fprintf(stderr, "%s: ", prog_name);
    }
    fprintf(stderr, "%s\n", msg);
}

int main(int argc, char *argv[]) {
    char *prog_name = argv[0];
    lua_State *L = luaL_newstate();
    if (!L) {
        write(prog_name, "insufficient memory to start lua");
        return EXIT_FAILURE;
    }
    if (argc < 2) {
        write(prog_name, "expected <file name>");
        return EXIT_FAILURE;
    }
    int err_code = luaL_loadfile(L, argv[1]);
    lua_close(L);
    return err_code;
}
