
# LuaJ

LuaJ is a toy tracing just-in-time (JIT) compiler for the [Lua](https://www.lua.org/) programming language. It's designed to be a binary drop-in replacement for the standard Lua distribution.

LuaJ is mostly written for my own educational benefit and isn't designed to replace [LuaJIT](https://luajit.org/). LuaJ will only support an x86-64 backend, and won't have an interpreter hand-coded in assembly.

## Building

LuaJ uses [CMake](https://cmake.org/). Clone the repository and build LuaJ from its source using:

```bash
$ git clone https://github.com/benanders/LuaJ
$ cd LuaJ
$ mkdir cmake-build-debug
$ cd cmake-build-debug
$ cmake -DCMAKE_BUILD_TYPE=Release ..
$ make
```
