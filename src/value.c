
#include <string.h>

#include "value.h"

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
