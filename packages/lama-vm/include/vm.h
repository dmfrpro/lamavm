#ifndef LAMA_VM_H
#define LAMA_VM_H

#define _GNU_SOURCE
#include "runtime.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

enum bytecode_opcode {
	BC_BINOP_ADD = 0x01,
	BC_BINOP_SUB = 0x02,
	BC_BINOP_MUL = 0x03,
	BC_BINOP_DIV = 0x04,
	BC_BINOP_MOD = 0x05,
	BC_BINOP_LT = 0x06,
	BC_BINOP_LE = 0x07,
	BC_BINOP_GT = 0x08,
	BC_BINOP_GE = 0x09,
	BC_BINOP_EQ = 0x0A,
	BC_BINOP_NE = 0x0B,
	BC_BINOP_AND = 0x0C,
	BC_BINOP_OR = 0x0D,
	BC_BINOP_FIRST = BC_BINOP_ADD,
	BC_BINOP_LAST = BC_BINOP_OR,
	BC_CONST = 0x10,
	BC_STRING = 0x11,
	BC_SEXP = 0x12,
	BC_STI = 0x13,
	BC_STA = 0x14,
	BC_JMP = 0x15,
	BC_END = 0x16,
	BC_RET = 0x17,
	BC_DROP = 0x18,
	BC_DUP = 0x19,
	BC_SWAP = 0x1A,
	BC_ELEM = 0x1B,
	BC_LD_FIRST = 0x20,
	BC_LD_LAST = 0x23,
	BC_LDA_FIRST = 0x30,
	BC_ST_FIRST = 0x40,
	BC_VAR_GROUP_MASK = 0xF0,
	BC_VAR_KIND_MASK = 0x0F,
	BC_LDA_GROUP = 0x30,
	BC_CJMPZ = 0x50,
	BC_CJMPNZ = 0x51,
	BC_BEGIN = 0x52,
	BC_BEGINC = 0x53,
	BC_CLOSURE = 0x54,
	BC_CALLC = 0x55,
	BC_CALL = 0x56,
	BC_TAG = 0x57,
	BC_ARRAY = 0x58,
	BC_FAIL = 0x59,
	BC_FAILC = 0x5A,
	BC_LINE = 0x5B,
	BC_PATT_FIRST = 0x60,
	BC_PATT_LAST = 0x66,
	BC_BARRAY = 0x74,
	BC_EOF = 0xFF,
};

enum pattern_kind {
	PATT_STRING,
	PATT_STRING_TAG,
	PATT_ARRAY_TAG,
	PATT_SEXP_TAG,
	PATT_BOXED,
	PATT_UNBOXED,
	PATT_CLOSURE,
};

enum public_symbol_kind {
	PUB_FUNCTION,
	PUB_GLOBAL,
};

enum {
	I_BINOP_ADD,
	I_BINOP_SUB,
	I_BINOP_MUL,
	I_BINOP_DIV,
	I_BINOP_MOD,
	I_BINOP_LT,
	I_BINOP_LE,
	I_BINOP_GT,
	I_BINOP_GE,
	I_BINOP_EQ,
	I_BINOP_NE,
	I_BINOP_AND,
	I_BINOP_OR,
	I_CONST,
	I_STRING,
	I_SEXP,
	I_STI,
	I_STA,
	I_JMP,
	I_END,
	I_RET,
	I_DROP,
	I_DUP,
	I_SWAP,
	I_ELEM,
	I_LD_GLO,
	I_LD_LOC,
	I_LD_ARG,
	I_LD_CLO,
	I_LDA,
	I_ST_GLO,
	I_ST_LOC,
	I_ST_ARG,
	I_ST_CLO,
	I_CJMPZ,
	I_CJMPNZ,
	I_BEGIN,
	I_BEGINC,
	I_CLOSURE,
	I_CALLC,
	I_CALL,
	I_TAG,
	I_ARRAY,
	I_FAIL,
	I_LINE,
	I_PATT,
	I_BARRAY,
	I_EOF
};

enum { K_GLO = 0, K_LOC = 1, K_ARG = 2, K_CLO = 3 };

struct capture {
	uint8_t kind;
	int32_t index;
	aint *glob;
};

struct builtin;
struct vm;
struct insn;

struct code_ref {
	size_t unit_id;
	int32_t offset;
};

typedef struct code_ref (*opcode_handler)(struct vm *vm, struct insn *in);

struct insn {
	opcode_handler handler;
	uint8_t op;
	uint8_t kind;
	int32_t a, b;
	aint imm;
	int32_t str;
	struct code_ref next;
	struct code_ref target;
	size_t caps;
	aint *glob;
	const struct builtin *bi;
	int32_t raw;
	size_t unit_id;
};

struct pubsym {
	int32_t name;
	int32_t offset;
	uint8_t flag;
	int32_t entry;
	aint *glob;
	size_t unit_id;
};

struct unit {
	size_t id;
	size_t name;
	size_t path;
	size_t image;
	size_t image_size;
	size_t strings_size;
	size_t globals_count;
	aint *globals;
	size_t imports_count;
	size_t imports;
	size_t pubs_count;
	size_t pubs;
	size_t code_offset;
	size_t code_size;
	int validated;
};

#endif
