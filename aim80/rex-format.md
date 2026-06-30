# REX — Relocatable EXtended Object Format

## Overview

REX is a byte-aligned relocatable object format for 8080/Z80 systems. It is designed as a modern replacement for the Microsoft LINK-80 `.REL` bitstream format, carrying the same semantic information with improved extensibility, longer symbol names, and easier parsing.

REX files use the `.rex` extension by convention.

## Design Goals

- **Byte-aligned** — no bit-packing; every field starts on a byte boundary
- **Self-describing** — magic number and version for format auto-detection
- **Extensible** — tagged records with length fields; unknown types can be skipped
- **Compatible** — same relocation model as .REL (segments, external chains, entry points)
- **Long symbols** — null-terminated names with no inherent length limit
- **Debuggable** — easy to hexdump and inspect

## File Structure

A REX file consists of a fixed header followed by a sequence of records, terminated by an EOF record.

```
+--------+--------+--------+--------+--------+--------+
| 'R'    | 'E'    | 'X'    | ver    | flags  | adrsz  |
+--------+--------+--------+--------+--------+--------+
| record | record | record | ...    | EOF record      |
+--------+--------+--------+--------+-----------------+
```

### Header (6 bytes)

| Offset | Size | Field    | Description                              |
|--------|------|----------|------------------------------------------|
| 0      | 3    | magic    | ASCII `REX` (0x52, 0x45, 0x58)           |
| 3      | 1    | version  | Format version (currently 0x01)          |
| 4      | 1    | flags    | Bit flags (see below)                    |
| 5      | 1    | adrsz    | Address size: 2 = 16-bit, 3 = 24-bit    |

#### Flags (byte 4)

| Bit | Meaning                          |
|-----|----------------------------------|
| 0   | File contains debug info records |
| 1   | File contains line number records|
| 2-7 | Reserved (must be 0)             |

### Records

Each record has a 3-byte header followed by a variable-length payload:

```
+--------+--------+--------+--- ... ---+
| type   | len_lo | len_hi | payload   |
+--------+--------+--------+--- ... ---+
```

| Field  | Size | Description                                     |
|--------|------|-------------------------------------------------|
| type   | 1    | Record type (see table below)                   |
| len    | 2    | Payload length in bytes (little-endian, 0-65535)|
| payload| len  | Record-specific data                            |

Tools must skip records with unknown type codes by reading and discarding `len` bytes of payload. This ensures forward compatibility.

## Segment Types

Several records contain a segment type byte. The encoding is:

| Value | Segment    | Description                    |
|-------|------------|--------------------------------|
| 0     | Absolute   | Non-relocatable                |
| 1     | Code       | Code-relative (CSEG)           |
| 2     | Data       | Data-relative (DSEG)           |
| 3     | Common     | Common-relative (COMMON block) |

## Record Types

### 0x01 — MODULE_NAME

Defines the module name.

| Field | Type   | Description                |
|-------|--------|----------------------------|
| name  | string | Null-terminated module name|

There should be exactly one MODULE_NAME record per file, appearing before any code records. The name is derived from the TITLE or NAME pseudo-op, or from the source filename.

### 0x02 — CODE_BYTES

A block of code or data bytes to be loaded at a specific offset in a segment.

| Field  | Type   | Description                          |
|--------|--------|--------------------------------------|
| seg    | u8     | Segment type                         |
| offset | u16 LE | Starting offset within the segment   |
| data   | bytes  | One or more bytes (len - 3 bytes)    |

Multiple consecutive bytes in the same segment are coalesced into a single CODE_BYTES record for efficiency. The assembler may emit multiple CODE_BYTES records for the same segment if there are gaps (e.g., after ORG or segment switches).

### 0x03 — SET_LOC

Sets the location counter within a segment. Equivalent to the ORG pseudo-op.

| Field  | Type   | Description                        |
|--------|--------|------------------------------------|
| seg    | u8     | Segment type                       |
| offset | u16 LE | New location counter value         |

### 0x04 — ENTRY_SYMBOL

Declares a public (entry) symbol — visible to the linker for inter-module references.

| Field | Type   | Description                          |
|-------|--------|--------------------------------------|
| name  | string | Null-terminated symbol name          |
| seg   | u8     | Segment type of the symbol's value   |
| value | u16 LE | Symbol value (offset within segment) |

### 0x05 — EXTERN_REF

Declares an external symbol reference chain. The linker uses this to patch all locations that reference the external.

| Field      | Type   | Description                                  |
|------------|--------|----------------------------------------------|
| name       | string | Null-terminated external symbol name         |
| seg        | u8     | Segment type of the chain addresses          |
| chain_head | u16 LE | Address of the last (most recent) reference  |

The external reference chain is a linked list embedded in the code. Each 16-bit reference location contains the address of the previous reference (0x0000 marks the end of the chain). The linker walks the chain, replacing each entry with the resolved symbol value plus any offset.

### 0x06 — CHAIN_ADDR

A chain address fixup — the linker replaces all entries in the chain with the current location counter.

| Field      | Type   | Description                        |
|------------|--------|------------------------------------|
| seg        | u8     | Segment type                       |
| chain_head | u16 LE | Head of the address chain          |

### 0x07 — COMMON_DEF

Defines a COMMON block and its size.

| Field | Type   | Description                          |
|-------|--------|--------------------------------------|
| name  | string | Null-terminated COMMON block name    |
| size  | u16 LE | Size of the COMMON block in bytes    |

An empty name (single null byte) represents blank COMMON.

### 0x08 — DATA_SIZE

Declares the total size of the data segment.

| Field | Type   | Description                    |
|-------|--------|--------------------------------|
| size  | u16 LE | Data segment size in bytes     |

### 0x09 — CODE_SIZE

Declares the total size of the code segment.

| Field | Type   | Description                    |
|-------|--------|--------------------------------|
| size  | u16 LE | Code segment size in bytes     |

### 0x0A — END_MODULE

Marks the end of the module. Optionally specifies a program start address.

| Field      | Type   | Description                                    |
|------------|--------|------------------------------------------------|
| has_start  | u8     | 1 if a start address is specified, 0 if not    |
| seg        | u8     | Segment type of the start address              |
| start_addr | u16 LE | Start address (meaningful only if has_start=1) |

### 0x0B — SELECT_COMMON

Selects a COMMON block for subsequent code emission.

| Field | Type   | Description                          |
|-------|--------|--------------------------------------|
| name  | string | Null-terminated COMMON block name    |

### 0x0C — LIB_SEARCH

Requests a library search for unresolved symbols (equivalent to .REQUEST).

| Field | Type   | Description                          |
|-------|--------|--------------------------------------|
| name  | string | Null-terminated library filename     |

### 0x0D — RELOC_FIXUP

Inter-segment word relocation. Informs the linker that a 16-bit word at a specific location holds a reference to another segment and must be patched at link time.

| Field  | Type   | Description                                    |
|--------|--------|------------------------------------------------|
| seg_at | u8     | Segment containing the word to patch           |
| off_at | u16 LE | Offset of the word within seg_at               |
| seg_to | u8     | Target segment the word references             |
| type   | u8     | 0=word, 1=low byte, 2=high byte                |
| value  | u16 LE | Pre-relocation value (offset within target)    |

The linker resolves this by computing `target_segment_base + value` and writing the result at `seg_at:off_at`. This record is emitted whenever code contains a 16-bit address pointing to a different segment (e.g., a CSEG instruction referencing a DSEG variable, or a COMMON-relative address in code).

### 0xFF — EOF

End of file marker. Payload length is 0. Must be the last record.

## Extension Records

These records provide capabilities beyond the original .REL format. Tools that do not understand them must skip them using the length field.

### 0x10 — LONG_SYMBOL

An extended symbol definition, used when symbol names exceed 6 characters and backward compatibility with .REL-only tools is desired. (In practice, ENTRY_SYMBOL and EXTERN_REF already support long names in REX, so this record is reserved for special cases.)

| Field | Type   | Description                                |
|-------|--------|--------------------------------------------|
| stype | u8     | 0 = public, 1 = external                   |
| name  | string | Null-terminated symbol name (any length)   |
| seg   | u8     | Segment type                               |
| value | u16 LE | Symbol value                               |

### 0x11 — SOURCE_FILE

Associates subsequent code with a source file. Used for debug information.

| Field | Type   | Description                          |
|-------|--------|--------------------------------------|
| name  | string | Null-terminated source file path     |

### 0x12 — LINE_NUMBER

Maps a source line number to a code offset. Used for source-level debugging.

| Field  | Type   | Description                        |
|--------|--------|------------------------------------|
| line   | u16 LE | Source line number                 |
| offset | u16 LE | Code offset within current segment |

### 0x13 — EXPR_FIXUP (reserved)

A deferred expression fixup using RPN (Reverse Polish Notation) encoding. This allows the linker to evaluate complex relocatable expressions that cannot be represented as simple chains.

| Field    | Type   | Description                              |
|----------|--------|------------------------------------------|
| offset   | u16 LE | Code offset to patch                     |
| expr_len | u8     | Length of the RPN expression in bytes    |
| expr     | bytes  | RPN-encoded expression                   |

RPN expression encoding (each element is 1 byte opcode + optional operand):

| Opcode | Operand    | Description          |
|--------|------------|----------------------|
| 0x01   | u16 LE     | Push literal value   |
| 0x02   | string     | Push symbol value    |
| 0x03   | u8 seg     | Push location counter|
| 0x10   | —          | Add                  |
| 0x11   | —          | Subtract             |
| 0x12   | —          | Multiply             |
| 0x13   | —          | Divide               |
| 0x14   | —          | Modulo               |
| 0x15   | —          | Negate               |
| 0x16   | —          | AND                  |
| 0x17   | —          | OR                   |
| 0x18   | —          | XOR                  |
| 0x19   | —          | NOT                  |
| 0x1A   | —          | SHL                  |
| 0x1B   | —          | SHR                  |
| 0x20   | —          | HIGH (high byte)     |
| 0x21   | —          | LOW (low byte)       |

*This record type is reserved for future implementation.*

### 0x14 — SECTION_ALIGN

Specifies an alignment requirement for a segment.

| Field     | Type | Description                                            |
|-----------|------|--------------------------------------------------------|
| seg       | u8   | Segment type                                           |
| alignment | u8   | Required alignment (power of 2, e.g., 2=word, 4=dword) |

### 0x15 — COMMENT

Arbitrary metadata or comment text. Tools should preserve but may ignore these.

| Field | Type   | Description                    |
|-------|--------|--------------------------------|
| text  | string | Null-terminated comment text   |

### 0x16 — BANK_SELECT (reserved)

Selects a memory bank for banked memory systems (MSX, CP/M 3, etc.).

| Field | Type | Description          |
|-------|------|----------------------|
| bank  | u8   | Bank number (0-255)  |

*This record type is reserved for future implementation.*

### 0xFF — EOF

Marks the end of the file. Payload length is 0.

## Comparison with .REL

| Feature                | .REL                    | .REX                      |
|------------------------|-------------------------|---------------------------|
| Alignment              | Bit-stream              | Byte-aligned              |
| Symbol name length     | 6 characters max        | Unlimited (null-terminated)|
| Format detection       | None (must know)        | Magic number `REX\x01`    |
| Extensibility          | Fixed record types      | Unknown types skippable   |
| Code emission          | 1 bit + 8 bits per byte | Bulk blocks               |
| Debug info             | Not supported           | Source file + line records|
| Expression fixups      | Not supported           | RPN expressions (reserved)|
| Section alignment      | Not supported           | SECTION_ALIGN record      |
| Memory banking         | Not supported           | BANK_SELECT (reserved)    |
| Typical file size      | Compact (bit-packed)    | ~1.5-2x larger            |

## Auto-Detection

A tool can distinguish .REL from .REX by reading the first 3 bytes:
- If they are `REX` (0x52, 0x45, 0x58), the file is REX format
- Otherwise, treat as .REL bitstream

## Example

Assembly source:
```
        TITLE   'HELLO'
        PUBLIC  MAIN
        EXTRN   PUTS
        CSEG
MAIN:   LXI     H,MSG
        CALL    PUTS
        RET
        DSEG
MSG:    DB      'Hi',0
        END     MAIN
```

Resulting REX file (annotated):
```
52 45 58 01 00 02          Header: REX v1, no flags, 16-bit
01 06 00 48 45 4C 4C 4F 00 MODULE_NAME: "HELLO"
02 06 00 01 00 00 21 xx xx CODE_BYTES: code seg, offset 0, LXI H,MSG
02 04 00 01 03 00 CD xx xx CODE_BYTES: code seg, offset 3, CALL PUTS
02 02 00 01 06 00 C9       CODE_BYTES: code seg, offset 6, RET
02 06 00 02 00 00 48 69 00 CODE_BYTES: data seg, offset 0, "Hi",0
09 02 00 07 00             CODE_SIZE: 7
08 02 00 03 00             DATA_SIZE: 3
04 07 00 4D 41 49 4E 00 01 00 00  ENTRY_SYMBOL: "MAIN", code, 0
05 07 00 50 55 54 53 00 01 03 00  EXTERN_REF: "PUTS", code, chain=3
0A 04 00 01 01 00 00       END_MODULE: has_start=1, code, addr=0
FF 00 00                   EOF
```

## Version History

| Version | Description                                    |
|---------|------------------------------------------------|
| 0x01    | Initial version. 16-bit addresses.             |

## Notes

- All multi-byte integers are little-endian.
- Strings are null-terminated and may contain any byte value except 0x00.
- The maximum payload length per record is 65535 bytes.
- Assemblers should emit records in this order: header, MODULE_NAME, code/data records, size records, symbol records, END_MODULE, EOF.
- The linker should process records sequentially but must tolerate records in any order (except header must be first and EOF must be last).
