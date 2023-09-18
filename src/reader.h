
#ifndef LUAJ_READER_H
#define LUAJ_READER_H

// A reader takes a file or string and spits out one character at a time. It
// optionally lets you peek at the next character (or the one after) without
// consuming it.
//
// It keeps track of the current line and column number for error messages.

#include <stdio.h>

#include "state.h"

#define MAX_CH_PEEK 3

typedef struct {
    State *L;
    lua_Reader fn;
    void *ud;
    char *p;
    size_t n; // Bytes remaining
    char *src_name; // Used for error/debug messages
    int line, col;
    char buf[MAX_CH_PEEK];
    int buf_len;
} Reader;

Reader reader_new(State *L, lua_Reader reader, void *ud, const char *src_name);

char read_ch(Reader *r);
void undo_ch(Reader *r, char c);
char peek_ch(Reader *r);
char peek_ch2(Reader *r);

#endif
