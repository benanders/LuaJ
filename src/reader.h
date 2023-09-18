
#ifndef LUAJ_READER_H
#define LUAJ_READER_H

// A reader takes a file or string and spits out one character at a time. It
// optionally lets you peek at the next character (or the one after) without
// consuming it.
//
// It keeps track of the current line and column number for error messages.

#include <stdio.h>

#define MAX_CH_PEEK 3

typedef struct {
    FILE *f;   // NULL if reading from string
    char *path;
    char *src; // NULL if reading from file
    int line, col;
    int buf[MAX_CH_PEEK];
    int buf_len;
} Reader;

Reader reader_from_str(char *src);
//Reader reader_from_file(char *path);

int read_ch(Reader *r);
void undo_ch(Reader *r, int c);
int peek_ch(Reader *r);
int peek_ch2(Reader *r);

#endif
