
#ifndef lua_h
#define lua_h

#include "luaconf.h"

#define LUA_VERSION_MAJOR	"5"
#define LUA_VERSION_MINOR	"4"
#define LUA_VERSION_RELEASE	"4"

#define LUA_VERSION_NUM			504
#define LUA_VERSION_RELEASE_NUM		(LUA_VERSION_NUM * 100 + 4)

#define LUA_VERSION	   "LuaJ " LUA_VERSION_MAJOR "." LUA_VERSION_MINOR
#define LUA_RELEASE	   LUA_VERSION "." LUA_VERSION_RELEASE
#define LUA_COPYRIGHT  LUA_RELEASE
#define LUA_AUTHORS	   "B. Anderson"

/* thread status; 0 is OK */
#define LUA_YIELD	1
#define LUA_ERRRUN	2
#define LUA_ERRSYNTAX	3
#define LUA_ERRMEM	4
#define LUA_ERRERR	5

typedef struct lua_State lua_State;

/*
** Type for memory-allocation functions
*/
typedef void * (*lua_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);

/*
** Type for functions that read/write blocks when loading/dumping Lua chunks
*/
typedef const char * (*lua_Reader) (lua_State *L, void *ud, size_t *sz);

/*
** state manipulation
*/
LUA_API lua_State *(lua_newstate) (lua_Alloc f, void *ud);
LUA_API void       (lua_close) (lua_State *L);

/*
** 'load' and 'call' functions (load and run Lua code)
*/
LUA_API int   (lua_load) (lua_State *L, lua_Reader reader, void *dt,
                          const char *chunkname);

#endif
