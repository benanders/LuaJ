
#ifndef LUAJ_BYTECODE_H
#define LUAJ_BYTECODE_H

#include <stdint.h>

// Bytecode instructions are 32 bits. Arguments are in the format:
//
//     xxxxxxxx  xxxxxxxx  xxxxxxxx  xxxxxxxx
//     Opcode--  A-------  B-------  C-------
//                         D-----------------
//               E---------------------------
//
// Opcodes are always 8 bits. Some instructions take 3 8-bit arguments, some
// take 1 8-bit and 1 16-bit, and JMP takes a single 24-bit bytecode offset.

#define BYTECODE       \
    X(NOP, 3)          \
                       \
    /* Storage */      \
    X(MOV, 3)          \
    X(KINT, 3)         \
    X(KNUM, 3)         \
    X(KPRIM, 3)        \
                       \
    /* Arithmetic */   \
    X(NEG, 3)          \
    X(ADDVV, 3)        \
    X(ADDVN, 3)        \
    X(SUBVV, 3)        \
    X(SUBVN, 3)        \
    X(SUBNV, 3)        \
    X(MULVV, 3)        \
    X(MULVN, 3)        \
    X(DIVVV, 3)        \
    X(DIVVN, 3)        \
    X(DIVNV, 3)        \
    X(MODVV, 3)        \
    X(MODVN, 3)        \
    X(MODNV, 3)        \
    X(POW, 3)          \
                       \
    /* Control flow */ \
    X(RET0, 3)

enum {
#define X(name, _) BC_ ## name,
    BYTECODE
    BC_LAST, // Marker for tables indexed by opcode
#undef X
};

typedef uint32_t BcIns;

static inline BcIns ins3(uint8_t op, uint8_t a, uint8_t b, uint8_t c) {
    return (BcIns) op | ((BcIns) a << 8) | ((BcIns) b << 16) | ((BcIns) c << 24);
}
static inline BcIns ins2(uint8_t op, uint8_t a, uint16_t d) {
    return (BcIns) op | ((BcIns) a << 8) | ((BcIns) d << 16);
}
static inline BcIns ins1(uint8_t op, uint32_t e) {
    return (BcIns) op | ((BcIns) e << 8);
}
static inline BcIns ins0(uint8_t op) { return (BcIns) op; }

static inline uint8_t  bc_op(BcIns ins) { return (uint8_t)  ins; }
static inline uint8_t  bc_a(BcIns ins)  { return (uint8_t)  (ins >> 8);  }
static inline uint8_t  bc_b(BcIns ins)  { return (uint8_t)  (ins >> 16); }
static inline uint8_t  bc_c(BcIns ins)  { return (uint8_t)  (ins >> 24); }
static inline uint16_t bc_d(BcIns ins)  { return (uint16_t) (ins >> 16); }
static inline uint32_t bc_e(BcIns ins)  { return (uint32_t) (ins >> 8);  }

static inline void bc_set_op(BcIns *ins, uint8_t op) {
    *ins = (*ins & 0xffffff00) | op;
}
static inline void bc_set_a(BcIns *ins, uint8_t a) {
    *ins = (*ins & 0xffff00ff) | ((BcIns) a << 8);
}
static inline void bc_set_b(BcIns *ins, uint8_t b) {
    *ins = (*ins & 0xff00ffff) | ((BcIns) b << 16);
}
static inline void bc_set_c(BcIns *ins, uint8_t c) {
    *ins = (*ins & 0x00ffffff) | ((BcIns) c << 24);
}
static inline void bc_set_d(BcIns *ins, uint16_t d) {
    *ins = (*ins & 0x0000ffff) | ((BcIns) d << 16);
}
static inline void bc_set_e(BcIns *ins, uint32_t e) {
    *ins = (*ins & 0x000000ff) | ((BcIns) e << 8);
}

#endif
