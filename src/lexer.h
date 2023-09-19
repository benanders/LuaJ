
#ifndef LUAJ_LEXER_H
#define LUAJ_LEXER_H

// A lexer takes a file or string reader and spits out tokens. A token is the
// smallest syntactical component of a Lua source file, like a symbol (e.g.,
// '=' or '*'), keyword (e.g., 'for'), identifier (e.g., 'hello'), or number
// (e.g., '234').
//
// A token mostly consists of its ID. For single character tokens, this is
// just its ASCII representation. For multi-character tokens or

#include "state.h"
#include "reader.h"
#include "value.h"

enum {
    // Symbols
    TK_EQ = 0x100, // First 256 characters are for ASCII
    TK_NEQ,
    TK_LE,
    TK_GE,
    TK_CONCAT,
    TK_VARARG,

    // Keywords
    TK_LOCAL,
    TK_FUNCTION,
    TK_IF,
    TK_ELSE,
    TK_ELSEIF,
    TK_THEN,
    TK_WHILE,
    TK_DO,
    TK_REPEAT,
    TK_UNTIL,
    TK_FOR,
    TK_END,
    TK_BREAK,
    TK_RETURN,
    TK_IN,
    TK_AND,
    TK_OR,
    TK_NOT,
    TK_NIL,
    TK_FALSE,
    TK_TRUE,

    // Values
    TK_IDENT,
    TK_NUM,
    TK_STR,
    TK_EOF,

    TK_LAST, // Marker for hash tables indexed by token
};

typedef struct {
    int t;
    char *chunk_name; // For error messages
    int line, col;    // For error messages
    union {
        double num; // TK_NUM
        Str *s;     // TK_IDENT
    };
} Token;

typedef struct {
    // The entire Lua state is needed by the lexer to be able to trigger errors
    // and allocate memory
    State *L;
    Reader *r;
    Token tk; // The most recently lexed token
} Lexer;

Lexer lexer_new(State *L, Reader *r);

// Optionally return info about the token via the struct in 'tk'
int read_tk(Lexer *l, Token *tk);
int peek_tk(Lexer *l, Token *tk);
void expect_tk(Lexer *l, int expected_tk, Token *tk);

#endif
