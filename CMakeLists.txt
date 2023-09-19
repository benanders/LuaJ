cmake_minimum_required(VERSION 3.23)
project(luaj C)

set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")

include_directories(include)

add_library(luajl
        src/lauxlib.c
        src/state.c src/state.h
        src/reader.c src/reader.h
        src/lexer.c src/lexer.h
        src/parser.c src/parser.h
        src/bytecode.h
        src/value.h src/value.c
        src/debug.c src/debug.h src/vm.c src/vm.h)

add_executable(luaj cli/main.c)
target_link_libraries(luaj luajl)
