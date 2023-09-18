
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "lexer.h"

#define MAX_TK_STR_LEN 10
#define FIRST_KEYWORD  TK_LOCAL

// These must be in the same order as they appear in the enum in 'lexer.h'
static char *KEYWORDS[] = {
    "local", "function", "if", "else", "elseif", "then", "while", "do",
    "repeat", "until", "for", "end", "break", "return", "in", "and", "or",
    "not", "nil", "false", "true",
};

typedef struct {
    char *s;
    int len, cap;
} StrBuf;

static StrBuf str_new() {
    StrBuf s = {0};
    s.cap = 8;
    s.s = malloc(sizeof(char) * s.cap);
    return s;
}

static void str_free(StrBuf *s) {
    free(s->s);
}

static void str_push(StrBuf *s, char c) {
    if (s->len + 1 >= s->cap) {
        s->cap *= 2;
        s->s = realloc(s->s, sizeof(char) * s->cap);
    }
    s->s[s->len++] = c;
}

Lexer lexer_new(Reader *r) {
    Lexer l = {0};
    l.r = r;
    read_tk(&l, NULL);
    return l;
}

// Copies the current source file/string name, line, and column position info
// from the reader to the token.
static void tk_new(Reader *r, TkInfo *tk) {
    if (!tk) {
        return;
    }
    *tk = (TkInfo) {0};
    tk->src_name = r->name;
    tk->line = r->line;
    tk->col = r->col;
}

static int lex_open_long_bracket(Lexer *l) {
    read_ch(l->r); // Skip first [
    int level = 0;
    int c = peek_ch(l->r);
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
    int c = read_ch(l->r);
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
//    if (c == EOF) {
//        TODO
//        err_syntax(l->L, l->tk, "unterminated block comment");
//    }
}

static void skip_line_comment(Lexer *l) {
    int c = read_ch(l->r);
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
    int c = peek_ch(l->r);
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
    StrBuf s = str_new();
    int c = read_ch(l->r);
    while (isalnum(c) || c == '_') {
        str_push(&s, (char) c);
        c = read_ch(l->r);
    }
    undo_ch(l->r, c);
    for (int i = 0; i < (int) (sizeof(KEYWORDS) / sizeof(KEYWORDS[0])); i++) {
        char *keyword = KEYWORDS[i];
        if (s.len == (int) strlen(keyword) &&
                strncmp(s.s, keyword, s.len) == 0) {
            l->tk = i + FIRST_KEYWORD;
            str_free(&s);
            return;
        }
    }
    l->tk = TK_IDENT;
    l->tk_info.name = s.s;
    l->tk_info.len = s.len;
}

static void lex_number(Lexer *l) {
    StrBuf s = str_new();
    int c = read_ch(l->r);
    int last = c;
    while (isalnum(c) || c == '.' || (strchr("eEpP", last) && strchr("+-", c))) {
        str_push(&s, (char) c);
        last = c;
        c = read_ch(l->r);
    }
    undo_ch(l->r, c);
    str_push(&s, '\0'); // strtod needs NULL terminator

    // Convert to number
    char *end;
    l->tk = TK_NUM;
    l->tk_info.num = strtod(s.s, &end);
    if (end - s.s != s.len - 1) { // -1 for the NULL terminator
        str_free(&s);
        // TODO
//        err_syntax(l->L, l->tk, "invalid symbol in number");
    }
    str_free(&s);
}

static void lex_symbol(Lexer *l) {
    int c = read_ch(l->r);
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
    l->tk = c;
}

static void next_tk(Lexer *l) {
    skip_spaces(l);
    tk_new(l->r, &l->tk_info);
    int c = peek_ch(l->r);
    if (c == EOF) {
        l->tk = TK_EOF;
    } else if (isalpha(c) || c == '_') {
        lex_keyword_or_ident(l);
    } else if (isdigit(c) || (c == '.' && isdigit(peek_ch2(l->r)))) {
        lex_number(l);
    } else {
        lex_symbol(l);
    }
}

int read_tk(Lexer *l, TkInfo *tk) {
    next_tk(l);
    if (tk) {
        *tk = l->tk_info;
    }
    return l->tk;
}

int peek_tk(Lexer *l, TkInfo *tk) {
    if (tk) {
        *tk = l->tk_info;
    }
    return l->tk;
}

static const char *TK_NAMES[] = {
    NULL, "'=='", "'!='", "'<='", "'>='", "'..'", "'...'",
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
        strcpy(dst, TK_NAMES[tk - 0xff]);
    }
}

int expect_tk(Lexer *l, int expected_tk, TkInfo *tk) {
    if (l->tk == expected_tk) {
        return read_tk(l, tk);
    } else {
        char expected[MAX_TK_STR_LEN];
        char found[MAX_TK_STR_LEN];
        tk2str(expected_tk, expected);
        tk2str(l->tk, found);
//        err_syntax(l->L, t, "expected %s, found %s", expected, found);
        // TODO
    }
}
