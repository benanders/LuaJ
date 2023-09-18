
#include <assert.h>

#include "reader.h"

Reader reader_new(State *L, lua_Reader reader, void *ud, const char *src_name) {
    Reader r = {0};
    r.L = L;
    r.fn = reader;
    r.ud = ud;
    r.src_name = (char *) src_name;
    r.line = 1;
    r.col = 1;
    return r;
}

static char read_ch_raw(Reader *r) {
    if (r->n == 0) {
        r->p = (char *) r->fn(r->L, r->ud, &r->n);
    }
    if (!r->p || *r->p == EOF) {
        return EOF;
    }
    char c = *(r->p++);
    r->n--;
    return c;
}

char read_ch(Reader *r) {
    char c;
    if (r->buf_len > 0) {
        c = r->buf[--r->buf_len];
    } else {
        c = read_ch_raw(r);
    }
    if (c == '\r') { // Turn '\r' into '\n'
        char c2 = read_ch_raw(r);
        if (c2 != '\n') { // Turn '\r\n' into '\n'
            undo_ch(r, c2);
        }
        c = '\n';
    }
    if (c == '\n') {
        r->line++;
        r->col = 1;
    } else if (c != EOF) {
        r->col++;
    }
    return c;
}

void undo_ch(Reader *r, char c) {
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

char peek_ch(Reader *r) {
    char c = read_ch(r);
    undo_ch(r, c);
    return c;
}

char peek_ch2(Reader *r) {
    char c1 = read_ch(r);
    char c2 = read_ch(r);
    undo_ch(r, c2);
    undo_ch(r, c1);
    return c2;
}
