
#ifndef LUAJ_LEXER_H
#define LUAJ_LEXER_H

// A lexer takes a file or string reader and spits out tokens. A token is the
// smallest syntactical component of a Lua source file, like a symbol (e.g.,
// '=' or '*'), keyword (e.g., 'for'), identifier (e.g., 'hello'), or number
// (e.g., '234').
//
// A token mostly consists of its ID. For single character tokens, this is
// just its ASCII representation. For multi-character tokens or

#include "reader.h"

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
    char *src_name; // For error messages
    int line, col;  // For error messages
    union {
        double num; // TK_NUM
        struct {    // TK_IDENT, TK_STR
            char *name;
            int len;
        };
    };
} TkInfo;

typedef struct {
    Reader *r;
    int tk;
    TkInfo tk_info;
} Lexer;

Lexer lexer_new(Reader *r);

// Optionally return info about the token via the struct in 'tk'
int read_tk(Lexer *l, TkInfo *tk);
int peek_tk(Lexer *l, TkInfo *tk);
int expect_tk(Lexer *l, int expected_tk, TkInfo *tk);

#endif
