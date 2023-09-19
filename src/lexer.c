
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "lexer.h"

#define MAX_TK_NAME_LEN 20
#define FIRST_KEYWORD   TK_LOCAL

// These must be in the same order as they appear in the enum in 'lexer.h'
static char *KEYWORDS[] = {
    "local", "function", "if", "else", "elseif", "then", "while", "do",
    "repeat", "until", "for", "end", "break", "return", "in", "and", "or",
    "not", "nil", "false", "true",
};

typedef struct {
    char *s;
    int len, max;
} StrBuf;

static StrBuf buf_new(State *L) {
    StrBuf s = {0};
    s.max = 8;
    s.s = mem_alloc(L, sizeof(char) * s.max);
    return s;
}

static void buf_free(State *L, StrBuf *s) {
    mem_free(L, s->s, sizeof(char) * s->max);
}

static void buf_push(State *L, StrBuf *s, char c) {
    if (s->len + 1 >= s->max) {
        s->s = mem_realloc(
                L,
                s->s,
                sizeof(char) * s->max,
                sizeof(char) * s->max * 2);
        s->max *= 2;
    }
    s->s[s->len++] = c;
}

Lexer lexer_new(State *L, Reader *r) {
    Lexer l = {0};
    l.L = L;
    l.r = r;
    read_tk(&l, NULL);
    return l;
}

// Copies the current source file/string name, line, and column position info
// from the reader to the token.
static void tk_new(Reader *r, Token *tk) {
    if (!tk) {
        return;
    }
    *tk = (Token) {0};
    tk->chunk_name = r->chunk_name;
    tk->line = r->line;
    tk->col = r->col;
}

static int lex_open_long_bracket(Lexer *l) {
    read_ch(l->r); // Skip first [
    int level = 0;
    char c = peek_ch(l->r);
    while (c == '=') {
        level++;
        read_ch(l->r);
        c = peek_ch(l->r);
    }
    if (peek_ch(l->r) != '[') { // Invalid long bracket
        return -1;
    }
    read_ch(l->r); // Skip second [
    return level;
}

static void skip_block_comment(Lexer *l, int level) {
    int n = -1;
    char c = read_ch(l->r);
    while (c != EOF) {
        if (n < 0 && c == ']') { // Terminator not yet started
            n = 0;
        } else if (n >= 0 && c == '=') { // Another level for terminator
            n++;
        } else if (n == level && c == ']') { // Terminator finished
            break;
        } else if (n >= 0) { // Not a valid terminator
            n = -1;
        }
        c = read_ch(l->r);
    }
    if (c == EOF) {
        ErrInfo info = tk2err(&l->tk);
        err_syntax(l->L, &info, "unterminated block comment");
    }
}

static void skip_line_comment(Lexer *l) {
    char c = read_ch(l->r);
    while (c != '\n' && c != EOF) {
        c = read_ch(l->r);
    }
}

static void skip_comment(Lexer *l) {
    read_ch(l->r); read_ch(l->r); // Skip '--'
    if (peek_ch(l->r) == '[') {
        int level = lex_open_long_bracket(l);
        if (level > 0) { // Has opening long bracket
            skip_block_comment(l, level);
            return;
        } // Don't have a long bracket, fall through to line comment...
    }
    skip_line_comment(l);
}

static void skip_spaces(Lexer *l) {
    char c = peek_ch(l->r);
    while (1) {
        if (c == '-' && peek_ch2(l->r) == '-') { // Comment
            skip_comment(l);
        } else if (isspace(c)) { // Whitespace
            read_ch(l->r);
        } else {
            break;
        }
        c = peek_ch(l->r);
    }
}

static void lex_keyword_or_ident(Lexer *l) {
    StrBuf s = buf_new(l->L);
    char c = read_ch(l->r);
    while (isalnum(c) || c == '_') {
        buf_push(l->L, &s, (char) c);
        c = read_ch(l->r);
    }
    undo_ch(l->r, c);
    for (int i = 0; i < (int) (sizeof(KEYWORDS) / sizeof(KEYWORDS[0])); i++) {
        char *keyword = KEYWORDS[i];
        if (s.len == (int) strlen(keyword) &&
                strncmp(s.s, keyword, s.len) == 0) {
            l->tk.t = i + FIRST_KEYWORD;
            buf_free(l->L, &s);
            return;
        }
    }
    l->tk.t = TK_IDENT;
    l->tk.s = str_new(l->L, s.s, s.len);
}

static void lex_number(Lexer *l) {
    StrBuf s = buf_new(l->L);
    char c = read_ch(l->r);
    char last = c;
    while (isalnum(c) || c == '.' || (strchr("eEpP", last) && strchr("+-", c))) {
        buf_push(l->L, &s, (char) c);
        last = c;
        c = read_ch(l->r);
    }
    undo_ch(l->r, c);
    buf_push(l->L, &s, '\0'); // strtod needs NULL terminator

    // Convert to number
    char *end;
    l->tk.t = TK_NUM;
    l->tk.num = strtod(s.s, &end);
    if (end - s.s != s.len - 1) { // -1 for the NULL terminator
        buf_free(l->L, &s);
        ErrInfo info = tk2err(&l->tk);
        err_syntax(l->L, &info, "invalid symbol in number");
    }
    buf_free(l->L, &s);
}

static void lex_symbol(Lexer *l) {
    int c = (int) read_ch(l->r);
    switch (c) {
    case '=': if (peek_ch(l->r) == '=') { read_ch(l->r); c = TK_EQ; }  break;
    case '~': if (peek_ch(l->r) == '=') { read_ch(l->r); c = TK_NEQ; } break;
    case '<': if (peek_ch(l->r) == '=') { read_ch(l->r); c = TK_LE; }  break;
    case '>': if (peek_ch(l->r) == '=') { read_ch(l->r); c = TK_GE; }  break;
    case '.':
        if (peek_ch(l->r) == '.' && peek_ch2(l->r) == '.') {
            read_ch(l->r); read_ch(l->r);
            c = TK_VARARG;
        } else if (peek_ch(l->r) == '.') {
            read_ch(l->r);
            c = TK_CONCAT;
        }
        break;
    default: break;
    }
    l->tk.t = c;
}

static void next_tk(Lexer *l) {
    skip_spaces(l);
    tk_new(l->r, &l->tk);
    char c = peek_ch(l->r);
    if (c == EOF) {
        l->tk.t = TK_EOF;
    } else if (isalpha(c) || c == '_') {
        lex_keyword_or_ident(l);
    } else if (isdigit(c) || (c == '.' && isdigit(peek_ch2(l->r)))) {
        lex_number(l);
    } else {
        lex_symbol(l);
    }
}

int read_tk(Lexer *l, Token *tk) {
    next_tk(l);
    if (tk) {
        *tk = l->tk;
    }
    return l->tk.t;
}

int peek_tk(Lexer *l, Token *tk) {
    if (tk) {
        *tk = l->tk;
    }
    return l->tk.t;
}

static const char *TK_NAMES[] = {
    "'=='", "'!='", "'<='", "'>='", "'..'", "'...'",
    "'local'", "'function'", "'if'", "'else'", "'elseif'", "'then'", "'while'",
    "'do'", "'repeat'", "'until'", "'for'", "'end'", "'break'", "'return'",
    "'in'", "'and'", "'or'", "'not'", "'nil'", "'false'", "'true'",
    "identifier", "number", "string", "end of file",
};

static void tk2str(int tk, char *dst) {
    if (tk <= 0xff) { // ASCII character
        dst[0] = '\'';
        dst[1] = (char) tk;
        dst[2] = '\'';
        dst[3] = '\0';
    } else { // Multi-character token
        strcpy(dst, TK_NAMES[tk - 0x100]);
    }
}

void expect_tk(Lexer *l, int expected_tk, Token *tk) {
    if (l->tk.t == expected_tk) {
        if (tk) {
            *tk = l->tk;
        }
        read_tk(l, NULL);
    } else {
        char expected[MAX_TK_NAME_LEN];
        char found[MAX_TK_NAME_LEN];
        tk2str(expected_tk, expected);
        tk2str(l->tk.t, found);
        ErrInfo info = tk2err(&l->tk);
        err_syntax(l->L, &info, "expected %s, found %s", expected, found);
    }
}

ErrInfo tk2err(Token *tk) {
    ErrInfo info = {0};
    info.chunk_name = tk->chunk_name;
    info.line = tk->line;
    info.col = tk->col;
    return info;
}
