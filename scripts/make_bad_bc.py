#!/usr/bin/env python3
"""Generate malformed .bc files (one per subdirectory-free name) into a directory.

Each case is a small mutation of a valid hand-written unit or a hand-built
invalid unit; scripts/test.py checks that lama-custom-vm rejects every one
of them with a diagnostic message and a non-zero exit code.
"""
import os
import struct
import sys


def hdr(strings=b"", globals_=0, imports=(), pubs=(), version=1, magic=b"LaMa"):
    return magic + struct.pack(
        "<iiiii", version, len(strings), globals_, len(imports), len(pubs)
    )


def i32(*vals):
    return b"".join(struct.pack("<i", v) for v in vals)


def valid_unit():
    """BEGIN 0 0; CONST 6; CONST 7; BINOP*; CALL write 1; END; EOF."""
    st = b"write\0"
    code = (
        bytes([0x52]) + i32(0, 0)
        + bytes([0x10]) + i32(6)
        + bytes([0x10]) + i32(7)
        + bytes([0x03])
        + bytes([0x56]) + i32(-1, 1)
        + bytes([0x16])
        + bytes([0xFF])
    )
    return hdr(st) + st + code


VALID = valid_unit()


def with_code(code, strings=b"", globals_=0, imports=(), pubs=()):
    return hdr(strings, globals_, imports, pubs) + strings + b"".join(
        i32(x) for x in imports
    ) + b"".join(
        i32(name_off, off) + bytes([flag]) for name_off, off, flag in pubs
    ) + code


def simple_body():
    return bytes([0x52]) + i32(0, 0) + bytes([0x10]) + i32(1) + bytes([0x16])


CASES = {}


def case(name):
    def deco(fn):
        CASES[name] = fn
        return fn
    return deco


@case("short_header")
def _():
    return b"LaMa" + b"\x00" * 10


@case("bad_magic")
def _():
    return b"XaMa" + VALID[4:]


@case("bad_version")
def _():
    return VALID[:4] + i32(99) + VALID[8:]


@case("negative_string_table_size")
def _():
    return VALID[:8] + i32(-1) + VALID[12:]


@case("negative_globals_count")
def _():
    return VALID[:12] + i32(-3) + VALID[16:]


@case("negative_imports_count")
def _():
    return VALID[:16] + i32(-1) + VALID[20:]


@case("negative_public_symbols_count")
def _():
    return VALID[:20] + i32(-1) + VALID[24:]


@case("sections_overflow_file")
def _():
    return VALID[:8] + i32(1 << 20) + VALID[12:]


@case("missing_code_section")
def _():
    return hdr(b"")


@case("code_without_eof")
def _():
    return with_code(simple_body())


@case("missing_eof_opcode")
def _():
    return with_code(simple_body() + bytes([0x5B]) + i32(-1))


@case("trailing_bytes_after_eof")
def _():
    return with_code(simple_body() + bytes([0xFF, 0x00, 0x00]))


@case("unknown_opcode")
def _():
    return with_code(bytes([0x52]) + i32(0, 0) + bytes([0x77, 0xFF]))


@case("truncated_operand")
def _():
    return with_code(bytes([0x52]) + i32(0, 0) + bytes([0x10, 0x01]))


@case("string_offset_out_of_range")
def _():
    return with_code(
        bytes([0x52]) + i32(0, 0) + bytes([0x11]) + i32(100) + bytes([0x16, 0xFF]),
        strings=b"abc\0",
    )


@case("string_not_terminated")
def _():
    return with_code(
        bytes([0x52]) + i32(0, 0) + bytes([0x11]) + i32(0) + bytes([0x16, 0xFF]),
        strings=b"abcd",
    )


@case("negative_string_offset")
def _():
    return with_code(
        bytes([0x52]) + i32(0, 0) + bytes([0x11]) + i32(-1) + bytes([0x16, 0xFF]),
        strings=b"abc\0",
    )


@case("external_reference_underflow")
def _():
    return with_code(
        bytes([0x52]) + i32(0, 0)
        + bytes([0x56]) + i32(-(1 << 31), 0)
        + bytes([0x16, 0xFF])
    )


@case("jump_outside_code")
def _():
    return with_code(bytes([0x52]) + i32(0, 0) + bytes([0x15]) + i32(1000) + bytes([0xFF]))


@case("jump_into_operand")
def _():
    # Target 12 points into the middle of the CONST instruction at offset 11.
    code = (
        bytes([0x52]) + i32(0, 0)          # 0..8
        + bytes([0x15]) + i32(12)          # 9..13: JMP 12 (inside itself)
        + bytes([0x16, 0xFF])
    )
    return with_code(code)


@case("call_target_not_begin")
def _():
    code = (
        bytes([0x52]) + i32(0, 0)          # 0: BEGIN
        + bytes([0x56]) + i32(9, 0)        # 9: CALL 9 (itself, not BEGIN)
        + bytes([0x16, 0xFF])
    )
    return with_code(code)


@case("negative_local_index")
def _():
    return with_code(
        bytes([0x52]) + i32(0, 1) + bytes([0x21]) + i32(-1) + bytes([0x16, 0xFF])
    )


@case("local_index_out_of_range")
def _():
    return with_code(
        bytes([0x52]) + i32(0, 1) + bytes([0x21]) + i32(5) + bytes([0x16, 0xFF])
    )


@case("global_index_out_of_range")
def _():
    return with_code(
        bytes([0x52]) + i32(0, 0) + bytes([0x20]) + i32(3) + bytes([0x16, 0xFF]),
        globals_=1,
    )


@case("closure_bad_capture_kind")
def _():
    code = (
        bytes([0x52]) + i32(0, 0)
        + bytes([0x54]) + i32(0, 1) + bytes([9]) + i32(0)
        + bytes([0x16, 0xFF])
    )
    return with_code(code)


@case("sexp_negative_field_count")
def _():
    code = (
        bytes([0x52]) + i32(0, 0)
        + bytes([0x12]) + i32(0, -2)
        + bytes([0x16, 0xFF])
    )
    return with_code(code, strings=b"Foo\0")


@case("undefined_external_function")
def _():
    code = (
        bytes([0x52]) + i32(0, 0)
        + bytes([0x56]) + i32(-1, 0)
        + bytes([0x16, 0xFF])
    )
    return with_code(code, strings=b"noSuchFunction\0")


@case("undefined_external_global")
def _():
    code = (
        bytes([0x52]) + i32(0, 0)
        + bytes([0x20]) + i32(-1)
        + bytes([0x16, 0xFF])
    )
    return with_code(code, strings=b"noSuchGlobal\0")


@case("missing_import")
def _():
    return with_code(
        simple_body() + bytes([0xFF]), strings=b"NoSuchModule\0", imports=[0]
    )


@case("public_symbol_bad_flag")
def _():
    return with_code(
        simple_body() + bytes([0xFF]), strings=b"f\0", pubs=[(0, 0, 7)]
    )


@case("public_global_out_of_range")
def _():
    return with_code(
        simple_body() + bytes([0xFF]), strings=b"g\0", globals_=1, pubs=[(0, 5, 1)]
    )


@case("public_function_not_begin")
def _():
    # Offset 9 is the CONST instruction, not a BEGIN.
    return with_code(
        simple_body() + bytes([0xFF]), strings=b"f\0", pubs=[(0, 9, 0)]
    )


@case("negative_begin_arg_count")
def _():
    return with_code(bytes([0x52]) + i32(-1, 0) + bytes([0x16, 0xFF]))


@case("duplicate_public_symbol")
def _():
    return with_code(
        simple_body() + bytes([0xFF]), strings=b"f\0", pubs=[(0, 0, 0), (0, 0, 0)]
    )


@case("binop_stack_underflow")
def _():
    return with_code(bytes([0x52]) + i32(0, 0) + bytes([0x01, 0x16, 0xFF]))


@case("drop_stack_underflow")
def _():
    return with_code(bytes([0x52]) + i32(0, 0) + bytes([0x18, 0x16, 0xFF]))


@case("swap_stack_underflow")
def _():
    code = (
        bytes([0x52]) + i32(0, 0)
        + bytes([0x10]) + i32(1)
        + bytes([0x1A, 0x16, 0xFF])
    )
    return with_code(code)


@case("end_stack_underflow")
def _():
    return with_code(bytes([0x52]) + i32(0, 0) + bytes([0x16, 0xFF]))


@case("division_by_zero")
def _():
    code = (
        bytes([0x52]) + i32(0, 0)
        + bytes([0x10]) + i32(1)
        + bytes([0x10]) + i32(0)
        + bytes([0x04, 0x16, 0xFF])
    )
    return with_code(code)


@case("modulo_by_zero")
def _():
    code = (
        bytes([0x52]) + i32(0, 0)
        + bytes([0x10]) + i32(1)
        + bytes([0x10]) + i32(0)
        + bytes([0x05, 0x16, 0xFF])
    )
    return with_code(code)


@case("end_extra_operands")
def _():
    code = (
        bytes([0x52]) + i32(0, 0)
        + bytes([0x10]) + i32(1)
        + bytes([0x10]) + i32(2)
        + bytes([0x16, 0xFF])
    )
    return with_code(code)


@case("closure_capture_count_mismatch")
def _():
    code = (
        bytes([0x52]) + i32(0, 1)       # 0: main BEGIN
        + bytes([0x54]) + i32(24, 1)    # 9: CLOSURE 24, one capture
        + bytes([1]) + i32(0)           # capture local 0
        + bytes([0x16])                 # 23: main END
        + bytes([0x53]) + i32(0, 0, 2)  # 24: BEGIN_CLOSURE, two captures
        + bytes([0x10]) + i32(1)
        + bytes([0x16, 0xFF])
    )
    return with_code(code)


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "build/negative"
    names = sys.argv[2:] or list(CASES)
    unknown = set(names) - CASES.keys()
    if unknown:
        raise SystemExit(f"unknown cases: {', '.join(sorted(unknown))}")
    os.makedirs(outdir, exist_ok=True)
    for name in names:
        with open(os.path.join(outdir, name + ".bc"), "wb") as f:
            f.write(CASES[name]())
    print(f"generated {len(names)} malformed units in {outdir}")


if __name__ == "__main__":
    main()
