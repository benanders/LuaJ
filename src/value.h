
#ifndef LUAJ_VALUE_H
#define LUAJ_VALUE_H

#include <lua.h>
#include <stdint.h>

#include "state.h"
#include "bytecode.h"

// ---- Values ----

// Values are stored as quiet NaNs. These are 64-bit doubles with all exponent
// bits set:
//
//   0b x1111111 111111xx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
//   = 0x 7ffc0000 00000000
//
// We can use the other 51 bits for whatever we want (including storing
// pointers, which are only 48 bits in a 64-bit address space).

#define QUIET_NAN ((uint64_t) 0x7ffc000000000000)

#define TAG_PRIM  (((uint64_t) 1) << 15)
#define TAG_NIL   (TAG_PRIM | 0b00)
#define TAG_FALSE (TAG_PRIM | 0b10)
#define TAG_TRUE  (TAG_PRIM | 0b11)
#define VAL_NIL   (QUIET_NAN | TAG_NIL)
#define VAL_FALSE (QUIET_NAN | TAG_FALSE)
#define VAL_TRUE  (QUIET_NAN | TAG_TRUE)

static inline uint64_t prim2v(uint16_t tag) {
    return QUIET_NAN | tag;
}

// A pointer is indicated by setting the sign bit (1 << 63). The lower 48 bits
// are the pointer value.
//
// Because we have 51 free bits and pointers only use 48, we can store the
// type of the object pointed to in the value itself to make type checking
// faster. Bit 63 is set to indicate a pointer value (TAG_PTR), so we have bits
// 48 and 49 to store up to 4 common types. TODO

#define TAG_PTR (((uint64_t) 1) << 63)
#define VAL_PTR (QUIET_NAN | TAG_PTR)

static inline uint64_t n2v(double n) {
    union { double n; uint64_t v; } cvt;
    cvt.n = n;
    return cvt.v;
}

static inline double v2n(uint64_t v) {
    union { double n; uint64_t v; } cvt;
    cvt.v = v;
    return cvt.n;
}

static inline void * v2ptr(uint64_t v) {
    return (void *) (v & 0xffffffffffff); // First 48 bits
}

static inline uint64_t ptr2v(void *ptr) {
    return ((uint64_t) ptr) | QUIET_NAN | TAG_PTR;
}

static inline int is_nil(uint64_t v)   { return v == VAL_NIL; }
static inline int is_false(uint64_t v) { return v == VAL_FALSE; }
static inline int is_true(uint64_t v)  { return v == VAL_TRUE; }
static inline int is_ptr(uint64_t v)   { return (v & VAL_PTR) == VAL_PTR; }


// ---- Objects ----

// GC-collected objects are all pointers to heap-allocated structs that begin
// with 'Header'. The header tells us the type of the object, as well as
// contains info for the GC.

enum {
    OBJ_STR,
    OBJ_FN,
};

#define ObjHeader uint8_t type; uint8_t _pad1; uint16_t _pad2; uint32_t _pad3;

// A GC-collected object.
typedef struct {
    ObjHeader;
} Obj;

static inline uint64_t obj2v(Obj *o)  { return ptr2v(o); }
static inline Obj * v2obj(uint64_t v) { return (Obj *) v2ptr(v); }
static inline int is_obj(uint64_t v, int type) {
    return is_ptr(v) && ((Obj *) v2obj(v))->type == type;
}

// Immutable string. The contents of the string is stored after the struct.
// The size of the whole object is 'sizeof(Str) + <length of string> + 1'.
typedef struct {
    ObjHeader;
    size_t len;
} Str;

Str * str_new(State *L, char *src, size_t len);
void str_free(State *L, Str *s);

static inline uint64_t str2v(Str *s)  { return ptr2v(s); }
static inline Str * v2str(uint64_t v) { return (Str *) v2ptr(v); }
static inline int is_str(uint64_t v)  { return is_obj(v, OBJ_STR); }
static inline char * str_val(Str *s)  { return (char *) (s + 1); }

// Function prototype.
typedef struct {
    ObjHeader;
    int num_params;
    BcIns *ins;
    int num_ins, max_ins;
    uint64_t *k;
    int num_k, max_k;
} Fn;

Fn * fn_new(State *L);
void fn_free(State *L, Fn *f);
int fn_emit(State *L, Fn *f, BcIns ins);
int fn_emit_k(State *L, Fn *f, uint64_t k);

static inline uint64_t fn2v(Fn *f)   { return ptr2v(f); }
static inline Fn * v2fn(uint64_t v)  { return (Fn *) v2ptr(v); }
static inline int is_fn(uint64_t v)  { return is_obj(v, OBJ_FN);  }

#endif
