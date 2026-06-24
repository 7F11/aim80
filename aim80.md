# aim80 — Enhancements over Microsoft MACRO-80

aim80 is a fully M80-compatible Z80/8080 assembler that produces bit-identical
.REL relocatable object files. In addition to complete M80 compatibility, it
provides the following extensions.

## Additional CPU Modes

M80 supports `.Z80` and `.8080` directives. aim80 adds:

| Directive   | CPU Mode     | Description                          |
|-------------|--------------|--------------------------------------|
| `.8080`     | Intel 8080   | Original M80 behavior                |
| `.8085`     | Intel 8085   | 8080 + RIM/SIM instructions          |
| `.Z80`      | Zilog Z80    | Original M80 behavior                |
| `.Z80UNDOC` | Z80 undocumented | Z80 + undocumented instructions |
| `.Z180`     | Zilog Z180   | Z80 + Z180 extensions                |
| `.R800`     | ASCII R800   | Z80 + MULUB/MULUW multiply           |
| `.ZXNEXT`   | ZX Next      | Z80 + undocumented + Next extensions |

CPU modes can be switched mid-file. Instructions are validated against
the active mode — using an instruction outside its mode produces an error.

### .8085 Instructions

| Mnemonic | Opcode | Description                     |
|----------|--------|---------------------------------|
| RIM      | 20     | Read Interrupt Mask             |
| SIM      | 30     | Set Interrupt Mask              |

### .Z80UNDOC Instructions

These are the commonly-used undocumented Z80 instructions that work on
all known Z80 silicon revisions.

**SLL — Shift Left Logical (set bit 0)**

| Mnemonic | Encoding | Description                    |
|----------|----------|--------------------------------|
| SLL r    | CB 30+r  | Shift left, bit 0 = 1         |

Registers: B, C, D, E, H, L, A.

**IXH/IXL/IYH/IYL — Index Register Halves**

The high and low bytes of IX and IY can be used as 8-bit registers
in LD, ADD, SUB, AND, OR, XOR, CP, INC, DEC instructions.

| Mnemonic      | Encoding    | Description               |
|---------------|-------------|---------------------------|
| LD IXH,n      | DD 26 nn    | Load immediate to IXH     |
| LD IXL,n      | DD 2E nn    | Load immediate to IXL     |
| LD r,IXH      | DD 44+r*8   | Copy IXH to register      |
| LD r,IXL      | DD 45+r*8   | Copy IXL to register      |
| LD IXH,r      | DD 60+r     | Copy register to IXH      |
| LD IXL,r      | DD 68+r     | Copy register to IXL      |

IYH/IYL use the FD prefix instead of DD. Registers r exclude H, L,
and (HL) — only B, C, D, E, A are valid.

### .Z180 Instructions

| Mnemonic   | Encoding      | Description                        |
|------------|---------------|------------------------------------|
| MLT BC     | ED 4C         | 8×8 multiply: B × C → BC          |
| MLT DE     | ED 5C         | D × E → DE                        |
| MLT HL     | ED 6C         | H × L → HL                        |
| MLT SP     | ED 7C         | SPH × SPL → SP                    |
| TST A,r    | ED 04+r*8     | Test (AND) A with register         |
| TST A,n    | ED 64 nn      | Test A with immediate              |
| TSTIO n    | ED 74 nn      | Test I/O port with immediate       |
| SLP        | ED 76         | Sleep until interrupt              |
| IN0 r,(n)  | ED 00+r*8, nn | Input from internal I/O port       |
| OUT0 (n),r | ED 01+r*8, nn | Output to internal I/O port        |
| OTIM       | ED 83         | Output, increment with memory      |
| OTDM       | ED 8B         | Output, decrement with memory      |
| OTIMR      | ED 93         | OTIM repeated                      |
| OTDMR      | ED 9B         | OTDM repeated                      |

### .R800 Instructions

The ASCII R800 (used in MSX turboR) is Z80 binary compatible with two
additional unsigned multiply instructions.

| Mnemonic     | Encoding  | Description                          |
|--------------|-----------|--------------------------------------|
| MULUB A,B    | ED C1     | A × B → HL (unsigned 8×8)           |
| MULUB A,C    | ED C9     | A × C → HL                          |
| MULUB A,D    | ED D1     | A × D → HL                          |
| MULUB A,E    | ED D9     | A × E → HL                          |
| MULUW HL,BC  | ED C3     | HL × BC → DE:HL (unsigned 16×16)    |
| MULUW HL,SP  | ED F3     | HL × SP → DE:HL                     |

## REX Output Format
### .ZXNEXT Instructions

The ZX Spectrum Next uses a custom FPGA Z80 core with extended instructions.
This mode also enables undocumented Z80 instructions (IXH/IXL/IYH/IYL, SLL).

| Mnemonic       | Encoding         | Description                       |
|----------------|------------------|-----------------------------------|
| MUL D,E        | ED 30            | 8×8 multiply: D × E → DE         |
| SWAPNIB        | ED 23            | Swap nibbles of A                 |
| MIRROR         | ED 24            | Mirror bits of A                  |
| TEST nn        | ED 27 nn         | AND A,nn (flags only, A unchanged)|
| NEXTREG r,n    | ED 91 rr nn      | Write n to Next register r        |
| NEXTREG r,A    | ED 92 rr         | Write A to Next register r        |
| PIXELDN        | ED 93            | Move HL down one pixel row        |
| PIXELAD        | ED 94            | Calculate pixel address from D,E  |
| SETAE          | ED 95            | Set A bit based on E[2:0]         |
| OUTINB         | ED 90            | OUT (C),(HL); HL++                |
| LDIX           | ED A4            | LDI without decrementing BC       |
| LDDX           | ED AC            | LDD without decrementing BC       |
| LDIRX          | ED B4            | LDIX repeated                     |
| LDDRX          | ED BC            | LDDX repeated                     |
| LDPIRX         | ED B7            | Pattern-aware block transfer      |
| LDIRSCALE      | ED B6            | Scaled block transfer             |
| BSLA DE,B      | ED 28            | Barrel shift left DE by B         |
| BSRA DE,B      | ED 29            | Barrel shift right arithmetic     |
| BSRL DE,B      | ED 2A            | Barrel shift right logical        |
| BSRF DE,B      | ED 2B            | Barrel shift right fill           |
| BRLC DE,B      | ED 2C            | Barrel rotate left circular       |
| ADD HL,A       | ED 31            | HL += A (zero-extended)           |
| ADD DE,A       | ED 32            | DE += A                           |
| ADD BC,A       | ED 33            | BC += A                           |
| ADD HL,nn      | ED 34 nn nn      | HL += 16-bit immediate            |
| ADD DE,nn      | ED 35 nn nn      | DE += 16-bit immediate            |
| ADD BC,nn      | ED 36 nn nn      | BC += 16-bit immediate            |
| PUSH nn        | ED 8A hi lo      | Push 16-bit immediate (big-endian)|


aim80 can produce output in the REX byte-aligned relocatable object format
as an alternative to the traditional .REL bitstream. Use `-f rex` on the
command line. The REX format is documented separately in `rex-format.md`.

## Cross-Reference Listing

The `-x` option adds a cross-reference table to the listing file, showing
for each symbol the line numbers where it is defined (#) and referenced.
This replaces the separate CREF-80 utility from the M80 toolchain.

## Command Line

```
aim80 [options] <source[.mac]>
Options:
  -o outfile    Object output file (.rel or .rex)
  -f rel|rex    Output format (overrides auto-detection)
  -l lstfile    Generate listing file
  -p cpu        Initial CPU mode (default: 8080)
  -x            Include cross-reference in listing
```

Valid `-p` arguments (case-insensitive): `8080`, `8085`, `Z80`, `Z80U`,
`Z80UNDOC`, `Z180`, `R800`, `ZXNEXT`, `NEXT`.

Output format is guessed from the `-o` file extension (`.rex` → REX,
`.rel` or other → REL). Use `-f` to override.

## Symbol Names

Internal symbol names are significant to 16 characters (matching M80 3.44).
External and public symbol names are limited to 6 characters in .REL output
(format constraint). The REX format has no such limitation.

## Error Codes

aim80 uses the same single-character error codes as M80 in the listing:

    A — Argument error
    C — Conditional nesting error
    D — Reference to multiply-defined symbol
    E — External in illegal context
    M — Multiply defined symbol
    N — Number error (bad digit)
    O — Bad opcode or syntax
    P — Phase error (value changed between passes)
    Q — Questionable (trailing junk)
    R — Relocation error (relocatable value where absolute required)
    U — Undefined symbol
