
#include <assert.h>

#include "reader.h"

static Reader reader_new() {
    Reader r = {0};
    r.line = 1;
    r.col = 1;
    return r;
}

Reader reader_from_str(char *src) {
    Reader r = reader_new();
    r.src = src;
    return r;
}

//Reader reader_from_file(char *path) {
//    Reader r = reader_new();
//    r.path = path;
//    r.f = fopen(path, "r");
//    if (!r.f) {
//        error(s, "cannot open file '%s'", path);
//    }
//    return r;
//}

static int read_ch_from_file(Reader *r) {
    int c = getc(r->f);
    if (c == '\r') { // Turn '\r' into '\n'
        int c2 = getc(r->f);
        if (c2 != '\n') { // Turn '\r\n' into '\n'
            ungetc(c2, r->f);
        }
        c = '\n';
    }
    return c;
}

static int read_ch_from_str(Reader *r) {
    if (*r->src == '\0') {
        return EOF;
    }
    int c = (int) *(r->src++);
    if (c == '\r') { // Turn '\r' into '\n'
        if (*r->src == '\n') { // Turn '\r\n' into '\n'
            r->src++;
        }
        return '\n';
    }
    return c;
}

int read_ch(Reader *r) {
    int c;
    if (r->buf_len > 0) {
        c = r->buf[--r->buf_len];
    } else if (r->f) {
        c = read_ch_from_file(r);
    } else {
        assert(r->src);
        c = read_ch_from_str(r);
    }
    if (c == '\n') {
        r->line++;
        r->col = 1;
    } else if (c != -1) {
        r->col++;
    }
    return c;
}

void undo_ch(Reader *r, int c) {
    if (c == -1) {
        return;
    }
    assert(r->buf_len < MAX_CH_PEEK);
    r->buf[r->buf_len++] = c;
    if (c == '\n') { // Undo line and column update
        r->col = 1;
        r->line--;
    } else {
        r->col--;
    }
}

int peek_ch(Reader *r) {
    int c = read_ch(r);
    undo_ch(r, c);
    return c;
}

int peek_ch2(Reader *r) {
    int c1 = read_ch(r);
    int c2 = read_ch(r);
    undo_ch(r, c2);
    undo_ch(r, c1);
    return c2;
}
