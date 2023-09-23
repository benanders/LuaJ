
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "value.h"

#define MAX_VAL_LEN 200

static Obj * obj_new(State *L, uint8_t type, size_t bytes) {
    Obj *obj = mem_alloc(L, bytes);
    obj->type = type;
    obj->_pad1 = 0;
    obj->_pad2 = 0;
    obj->_pad3 = 0;
    return obj;
}

static void obj_free(State *L, Obj *obj, size_t bytes) {
    mem_free(L, obj, bytes);
}

Str * str_new(State *L, size_t len) {
    Str *str = (Str *) obj_new(L, OBJ_STR, sizeof(Str) + sizeof(char) * len);
    str->len = len;
    return str;
}

void str_free(State *L, Str *str) {
    obj_free(L, (Obj *) str, sizeof(Str) + sizeof(char) * str->len);
}

Fn * fn_new(State *L, Str *fn_name, char *chunk_name) {
    Fn *f = (Fn *) obj_new(L, OBJ_FN, sizeof(Fn));
    f->name = fn_name;
    f->chunk_name = chunk_name;
    f->start_line = f->end_line = -1;
    f->num_params = 0;
    f->num_ins = 0;
    f->max_ins = 64;
    f->ins = mem_alloc(L, sizeof(BcIns) * f->max_ins);
    f->line_info = mem_alloc(L, sizeof(int) * f->max_ins);
    f->num_k = 0;
    f->max_k = 16;
    f->k = mem_alloc(L, sizeof(uint64_t) * f->max_k);
    return f;
}

void fn_free(State *L, Fn *f) {
    mem_free(L, f->ins, sizeof(BcIns) * f->max_ins);
    mem_free(L, f->line_info, sizeof(int) * f->max_ins);
    mem_free(L, f->k, sizeof(uint64_t) * f->max_k);
    obj_free(L, (Obj *) f, sizeof(Fn));
}

int fn_emit(State *L, Fn *f, BcIns ins, int line) {
    if (f->num_ins >= f->max_ins) {
        f->ins = mem_realloc(
                L,
                f->ins,
                f->max_ins * sizeof(BcIns),
                f->max_ins * sizeof(BcIns) * 2);
        f->line_info = mem_realloc(
                L,
                f->line_info,
                f->max_ins * sizeof(int),
                f->max_ins * sizeof(int) * 2);
        f->max_ins *= 2;
    }
    f->line_info[f->num_ins] = line;
    f->ins[f->num_ins] = ins;
    return f->num_ins++;
}

int fn_emit_k(State *L, Fn *f, uint64_t k) {
    if (f->num_k >= f->max_k) {
        f->k = mem_realloc(
                L,
                f->k,
                f->max_k * sizeof(BcIns),
                f->max_k * sizeof(BcIns) * 2);
        f->max_k *= 2;
    }
    f->k[f->num_k] = k;
    return f->num_k++;
}

char * type_name(uint64_t v) {
    if (is_num(v)) {
        if (is_nan(v)) {
            return "NaN";
        } else {
            return "number";
        }
    } else if (is_nil(v)) {
        return "nil";
    } else if (is_false(v) || is_true(v)) {
        return "boolean";
    } else if (is_str(v)) {
        return "string";
    } else if (is_fn(v)) {
        return "function";
    } else {
        return "object";
    }
}

static int quote_ch(char *s, char ch) {
    switch (ch) {
    case '\\': return sprintf(s, "\\\\");
    case '\"': return sprintf(s, "\\\"");
    case '\'': return sprintf(s, "\\'");
    case '\a': return sprintf(s, "\\a");
    case '\b': return sprintf(s, "\\b");
    case '\f': return sprintf(s, "\\f");
    case '\n': return sprintf(s, "\\n");
    case '\r': return sprintf(s, "\\r");
    case '\t': return sprintf(s, "\\t");
    case '\v': return sprintf(s, "\\v");
    case 0:    return sprintf(s, "\\0");
    default:
        if (iscntrl(ch)) {
            return sprintf(s, "\\%03o", ch);
        } else {
            return sprintf(s, "%c", ch);
        }
    }
}

static char * quote_str(State *L, char *str, size_t len) {
    char *v = mem_alloc(L, len * 2 * sizeof(char));
    char *s = v;
    s += sprintf(s, "\"");
    for (size_t i = 0; i < len; i++) {
        s += quote_ch(s, str[i]);
    }
    s += sprintf(s, "\"");
    return v;
}

char * print_fn_name(State *L, Fn *f) {
    char *v = mem_alloc(L, MAX_VAL_LEN * sizeof(char));
    char *s = v;
    if (f->name) {
        s += sprintf(s, "%.*s", (int) f->name->len, str_val(f->name));
    } else {
        s += sprintf(s, "<unknown>");
    }
    if (f->chunk_name) {
        s += sprintf(s, "@%s", f->chunk_name);
    } else {
        s += sprintf(s, "@<unknown>");
    }
    if (f->start_line >= 1) {
        s += sprintf(s, ":%d", f->start_line);
    }
    if (f->end_line >= f->start_line) {
        s += sprintf(s, "-%d", f->end_line);
    }
    return v;
}

char * print_val(State *L, uint64_t v) {
    if (is_num(v)) {
        if (is_nan(v)) {
            return "NaN";
        } else {
            char *s = mem_alloc(L, MAX_VAL_LEN * sizeof(char));
            snprintf(s, MAX_VAL_LEN, "%g", v2n(v));
            return s;
        }
    } else if (is_nil(v)) {
        return "nil";
    } else if (is_false(v)) {
        return "false";
    } else if (is_true(v)) {
        return "true";
    } else if (is_str(v)) {
        Str *str = v2str(v);
        return quote_str(L, str_val(str), str->len);
    } else if (is_fn(v)) {
        return print_fn_name(L, v2fn(v));
    } else {
        char *s = mem_alloc(L, MAX_VAL_LEN * sizeof(char));
        snprintf(s, MAX_VAL_LEN, "object <%p>", v2ptr(v));
        return s;
    }
}
