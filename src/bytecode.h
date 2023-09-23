
#ifndef LUAJ_BYTECODE_H
#define LUAJ_BYTECODE_H

#include <stdint.h>

// Bytecode instructions are 32 bits with an 8-bit opcode. Different
// instructions have different operands. Instructions can have either three
// 8-bit operands (A, B, and C), one 8-bit (A) and one 16-bit (D) operand, or
// one 24-bit operand (E).
//
//     xxxxxxxx  xxxxxxxx  xxxxxxxx  xxxxxxxx
// 1)  opcode--  A-------  B-------  C-------
// 2)  opcode--  A-------  D-----------------
// 3)  opcode--  E---------------------------
//
// The possible operand types are as follows. The opcode suffix letters (e.g.,
// 'VV', 'VP', 'NV', etc.) specify the types of the operands.
//
// * var (V):  A stack slot
// * prim (P): A primitive type tag ('TAG_NIL', 'TAG_FALSE', or 'TAG_TRUE')
// * num (N):  An index into the current function's constants table ('fn->k')
//             representing a constant floating-point number
// * int:      A 16-bit signed integer
// * str (S):  An index into the current function's constants table ('fn->k')
//             representing an immutable string object
// * func (F): An index into the current function's constants table ('fn->k')
//             representing a function prototype object
//
// Bytecode specification:
//
//   NOP       No operation; used to delete instructions on-the-fly in parsing
//
//
// -- Storage Operations --
//
//   MOV       Copies a value from a source to a destination stack slot
//             A -- Destination stack slot to copy to
//             D -- Source stack slot to copy from
//
//   KPRIM     Sets a stack slot to a primitive value
//             A -- Destination stack slot
//             D -- Primitive tag (one of 'TAG_NIL', 'TAG_FALSE', or 'TAG_TRUE')
//
//   KINT      Sets a stack slot to a number value
//             A -- Destination stack slot
//             D -- 16-bit signed integer (converted to a double at runtime)
//
//   KNUM      Sets a stack slot to a number value from the constants table
//             A -- Destination stack slot
//             D -- 16-bit unsigned index into the function's constants table
//
//   KSTR      Sets a stack slot to a string object from the constants table
//             A -- Destination stack slot
//             D -- 16-bit unsigned index into the function's constants table
//
//   KFN       Sets a stack slot to a function object from the constants table
//             A -- Destination stack slot
//             D -- 16-bit unsigned index into the function's constants table
//
//   KNIL      Sets stack slots 'A' through 'D' to nil
//             A -- First stack slot to set to nil
//             D -- Last stack slot to set to nil
//
//
// -- Arithmetic Operations --
//
//   NEG       Negates the value in stack slot 'D' and stores the result in 'A'
//             A -- Destination stack slot
//             D -- Stack slot of the number value to negate
//
//   ADDVV     Adds 'B' and 'C' and stores the result in 'A'
//             A -- Destination stack slot
//             B -- Left operand; stack slot (a number)
//             C -- Right operand; stack slot (a number)
//
//   ADDVN     Adds 'B' and 'C' and stores the result in 'A'
//             A -- Destination stack slot
//             B -- Left operand; stack slot (a number)
//             C -- Right operand; 8-bit unsigned index into constants table
//
// ...the remaining arithmetic operations are self-explanatory...
//
//   POW       Calculates 'B^C' and stores the result in 'A'
//             A -- Destination stack slot
//             B -- Left operand; stack slot (a number)
//             C -- Right operand; stack slot (a number)
//
//   CONCAT    Concatenates the strings in stack slots 'B' through 'C' and
//             stores the result in 'A'
//             A -- Destination stack slot
//             B -- First stack slot to concatenate (a string)
//             C -- Last stack slot to concatenate (a string)
//
//
// -- Conditional Operations --
//
// Conditional jumps consist of two instructions: a conditional instruction
// followed immediately by a 'JMP' instruction. An unconditional jump consists
// of just a 'JMP' instruction without a preceding condition.
//
//   NOT       Sets 'A' to the boolean not value of stack slot 'D'
//             A -- Destination stack slot to store the result in
//             D -- Stack slot
//
//   IST       Jumps if the value in 'D' is true
//             A -- Unused
//             D -- Stack slot
//
//   ISF       Jumps if the value in 'D' is false
//             A -- Unused
//             D -- Stack slot
//
//   ISTC      If the value in 'D' is true, then copies 'D' to 'A' and jumps
//             A -- Destination stack slot for the conditional copy
//             D -- Stack slot
//
//   ISFC      If the value in 'D' is false, then copies 'D' to 'A' and jumps
//             A -- Destination stack slot for the conditional copy
//             D -- Stack slot
//
//   EQVV      Compares 'A' and 'D' and jumps if they're equal
//             A -- Left operand; stack slot
//             D -- Right operand; stack slot
//
// ...the remaining conditional operations are self-explanatory...
//
//
// -- Control Flow Operations --
//
//   JMP       Jumps to a target instruction.
//             E -- 24-bit signed jump offset, relative to the PC of the
//                  instruction AFTER the 'JMP' instruction
//
//   CALL      Calls the function in stack slot 'A' with 'B' arguments in
//             contiguous stack slots immediately after 'A'. Expects 'C' return
//             values:
//                 A, A+1, ..., A+C-1 = A(A+1, ..., A+B-1)
//             A -- Stack slot of the function to call
//             B -- Number of arguments
//             C -- Number of return values
//
//   RET0      Returns from a function with no return values
//                 return
//
//   RET1      Returns one value from a function
//                 return D
//             D -- Stack slot of value to return
//
//   RET       Returns two or more values from a function (must be in
//             contiguous stack slots)
//                 return A, A+1, ..., A+D-1
//             A -- Stack slot of first value to return
//             D -- Number of values to return

// Jump offsets are stored as 24-bit signed values, calculated by:
//
//   E = target PC - jump PC + JMP_BIAS
//   target PC = jmp PC + E - JMP_BIAS
//
// Where E is the 24-bit E argument for the 'JMP' instruction. We opt for
// adding and subtracting a JMP_BIAS instead of storing the offset as a signed
// twos-complement integer because sign-extending a 24-bit value to 32-bits is
// computationally more expensive than a subtraction.
#define JMP_BIAS 0x800000

#define BYTECODE       \
    X(NOP, 0)          \
    X(ASSERT, 1)       \
                       \
    /* Storage */      \
    X(MOV, 2)          \
    X(KPRIM, 2)        \
    X(KINT, 2)         \
    X(KNUM, 2)         \
    X(KSTR, 2)         \
    X(KFN, 2)          \
    X(KNIL, 2)         \
                       \
    /* Arithmetic */   \
    X(NEG, 1)          \
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
    X(CONCAT, 3)       \
                       \
    /* Conditions */   \
    X(NOT, 2)          \
    X(IST, 1)          \
    X(ISTC, 2)         \
    X(ISF, 1)          \
    X(ISFC, 2)         \
    X(EQVV, 2)         \
    X(EQVP, 2)         \
    X(EQVN, 2)         \
    X(EQVS, 2)         \
    X(NEQVV, 2)        \
    X(NEQVP, 2)        \
    X(NEQVN, 2)        \
    X(NEQVS, 2)        \
    X(LTVV, 2)         \
    X(LTVN, 2)         \
    X(LEVV, 2)         \
    X(LEVN, 2)         \
    X(GTVV, 2)         \
    X(GTVN, 2)         \
    X(GEVV, 2)         \
    X(GEVN, 2)         \
                       \
    /* Control Flow */ \
    X(JMP, 1)          \
    X(CALL, 3)         \
    X(RET0, 0)         \
    X(RET1, 1)         \
    X(RET, 2)

enum {
#define X(name, _) BC_ ## name,
    BYTECODE
#undef X
    BC_LAST, // Marker for tables indexed by opcode
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
