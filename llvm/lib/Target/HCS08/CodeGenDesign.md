# HCS08 Code Generation — Design Plan

Status: Phases 0 and 1 are implemented; Phase 2 has not started. The MC layer
(assembler, disassembler, relocations, object emission) is complete and
tested. This document plans the code generator; §13 records where the result
diverged from the plan.

## 1. The core problem

HCS08 is one of the hardest shapes of machine for LLVM's codegen model:

| Resource | Reality |
|---|---|
| 8-bit compute registers | **One** — the accumulator `A`. Every ALU op reads/writes `A`. |
| 16-bit registers | **One** — the index `H:X` (also the only pointer/index register; `X` and `H` are its halves). |
| Stack pointer | `SP` (16-bit), grows down; only `ais #imm8`, `tsx`/`txs`, and the `9E` `,sp` addressing modes touch it. |
| Flags | `CCR` — clobbered by almost every instruction. |
| GP register file | **none** — real HC08 compilers use the **direct page** (zero-page equivalent) as the register file. |

LLVM's register allocator assumes a pool of interchangeable general-purpose
registers. HCS08 effectively has *one accumulator*. This single fact drives
every architectural decision below. The MC layer gives us correct encodings;
codegen is the hard 80%.

## 2. The central decision: the register model

Two viable models, and the recommendation is to commit to **both, in sequence**.

**Model A — real registers only (`A`, `H:X`), everything else in memory.**
Model just the hardware registers; force all other values into stack / direct-page
slots. Every operation becomes load->A, op, store. LLVM's allocator spills
`A` / `H:X` as needed.
- Correct, simple, matches LLVM's framework directly, fast to stand up.
- Terrible code (constant reload of `A`); the allocator has almost no freedom.

**Model B — imaginary direct-page register file (the llvm-mos approach).**
Reserve a window of the direct page (e.g. `__rc0..__rcN`, linker-provided) and
define a bank of *imaginary* 8-bit / 16-bit registers there. The allocator
treats them as real GP registers; a late pass rewrites them to concrete
direct-page addresses; `A` / `H:X` become transient "operating" registers
threaded through by pseudo-expansion.
- This is how every *usable* accumulator-machine backend works (llvm-mos /
  6502); it matches how CodeWarrior / SDCC use zero page. Good code.
- Research-grade complexity: custom regalloc interaction, a direct-page
  allocation pass, heavy pseudo expansion.

**Recommendation:** Model A to reach a *correct*, end-to-end compiler, then
migrate the register classes to Model B for quality. Model A code is throwaway
on the allocation side, but everything else (ISel patterns, ABI, frame,
AsmPrinter, libcalls) carries straight over.

## 3. Register `.td` rework (needed before any codegen)

The current classes are MC-shaped and wrong for allocation:
- `GR16 = (add HX, SP)` — `SP` must **not** be allocatable.
- `GR8  = (add A, X, H)` — `X` / `H` are the halves of the pointer register;
  freely allocating them fights pointer use.
- `H:X` needs proper **sub-register** modeling so a write to `X` or `H` aliases
  `HX`.

Concrete rework:

```tablegen
// sub-registers so the allocator / verifier understand aliasing
def X  : Register<"x">;
def H  : Register<"h">;
def HX : RegisterWithSubRegs<"hx", [H, X]> { let SubRegIndices = [sub_hi, sub_lo]; }

def ACC8  : RegisterClass<"HCS08", [i8],  8, (add A)>;    // accumulator
def IDX16 : RegisterClass<"HCS08", [i16], 8, (add HX)>;   // pointer / index
def CCRC  : RegisterClass<"HCS08", [i8],  8, (add CCR)> {
  let CopyCost = -1; let isAllocatable = 0;
}
// Phase 2 adds:  ZP8 (imaginary 8-bit),  ZP16 (imaginary 16-bit pairs)
```

`SP` becomes a reserved, non-allocatable register.

## 4. Target ABI (we define it — there is no HCS08 psABI)

Document a new ABI (like the LLVM-defined relocation set, this is ours to
specify):

- **Return values:** `i8` -> `A`; `i16` / pointer -> `H:X`; larger -> `sret`
  (hidden pointer argument).
- **Arguments (bring-up):** *all on the stack*, caller-pushed, accessed via
  frame indices -> `n,sp` (`9E`) addressing. Simpler `LowerCall` /
  `LowerFormalArguments`, with no register/stack mixing.
- **Arguments (later optimization):** first pointer / `i16` in `H:X`, first
  `i8` in `A`, remainder on stack.
- **Caller / callee-saved:** with so few registers, `A` / `H:X` are
  caller-saved; the direct-page imaginary registers (Phase 2) split into
  caller- and callee-saved banks.

This lives in `HCS08CallingConv.td` (`CC_HCS08`, `RetCC_HCS08`), but the
register scarcity means a lot happens in custom C++ (`LowerCCCArguments` /
`LowerCCCCallTo`), as on MSP430.

## 5. Frame lowering

- **No frame pointer by default.** Static frames use `SP`-relative `9E` `,sp`
  addressing; frame indices resolve to `SP` offsets in `eliminateFrameIndex`.
- **Prologue:** `ais #-frameSize`. **Epilogue:** `ais #frameSize` ; `rts`.
- **`H:X` as frame base** only when needed (dynamic `alloca` / VLA): `tsx`
  (`H:X <- SP+1`), access via `n,x`. Costs the one index register, so avoid
  unless required.
- **Call-frame pseudos** `ADJCALLSTACKDOWN` / `ADJCALLSTACKUP` -> `ais`.
- **Watch:** the `n,sp` offset is 8-bit (`SP1`) or 16-bit (`SP2`); frames
  larger than 255 bytes need `SP2` or an `H:X` base. Big-endian byte order
  matters for every multi-byte slot.

## 6. Type legalization

| Type | Strategy |
|---|---|
| `i1` | promote to `i8` |
| `i8` | legal (`A` / ZP) |
| `i16` / pointer | legal (`H:X` / ZP pairs); add/sub expand to `ADD`+`ADC` byte chains; `aix` for pointer +/- imm |
| `i32` / `i64` | expand to `i16` / `i8` chains or libcalls |
| `mul i8` | hardware `MUL` (`A x X -> X:A`) |
| `udiv` / `urem` (16/8) | hardware `DIV` (`H:A / X -> A`, remainder `H`); else libcall |
| variable shifts, wide mul/div | libcalls (compiler-rt-style; the runtime library is its own deliverable) |
| `float` / `double` | soft-float libcalls |

## 7. Instruction selection

- **Target SDNodes:** `HCS08ISD::{RET_GLUE, CALL, CMP, BR_CC, SELECT_CC,
  Wrapper}` (`Wrapper` for global / blockaddress). Requires `-gen-sd-node-info`.
- **Add ISel patterns to the existing (currently pattern-less) MC
  instructions.**
- **Addressing-mode selection** via a `ComplexPattern SelectAddr` choosing DIR
  (8-bit direct-page absolute) vs EXT (16-bit absolute) vs IX / IX1 / IX2
  (`H:X`-based) vs SP1 / SP2 (frame). This is the workhorse and where most ISel
  effort goes.
- **8-bit ALU** as two-address accumulator patterns (`A` tied dst/src):
  `(set A, (add A, addr))` -> `ADD`.
- **Compares** -> `CMP` / `CPX` / `CPHX` defining `CCR`; **branches** -> `Bcc`
  reading `CCR` (lower `BR_CC` / `SELECT_CC`, model `CCR` as a physical register
  with accurate `Defs = [CCR]` on every clobbering instruction — easy to get
  subtly wrong).
- **16-bit add/sub** custom-expanded to `LDA/ADD/STA` + `LDA/ADC/STA` byte
  chains (big-endian order).

## 8. Files to add (mirrors MSP430, HCS08-specific)

```
HCS08CallingConv.td          HCS08Subtarget.{h,cpp}
HCS08RegisterInfo.{h,cpp}    HCS08InstrInfo.{h,cpp}
HCS08FrameLowering.{h,cpp}   HCS08ISelLowering.{h,cpp}
HCS08ISelDAGToDAG.cpp        HCS08MachineFunctionInfo.{h,cpp}
HCS08AsmPrinter.cpp          HCS08MCInstLower.{h,cpp}
HCS08SelectionDAGInfo.{h,cpp}
HCS08.h  (pass decls)        HCS08TargetMachine.{h,cpp}  (add PassConfig + getSubtargetImpl)
```

Plus:
- `.td`: `HCS08RegisterInfo.td` rework, `HCS08InstrInfo.td` patterns / pseudos,
  include `HCS08CallingConv.td`.
- CMake: tablegen `-gen-dag-isel` / `-gen-callingconv` / `-gen-sd-node-info`;
  codegen sources; `LINK_COMPONENTS` (Analysis, AsmPrinter, CodeGen,
  SelectionDAG, GlobalISel, ...).
- Phase 2: `HCS08ZeroPageAlloc.cpp` + a pseudo-expansion pass.

## 9. Phased roadmap

- **Phase 0 — Skeleton (days).** All classes as minimal stubs; `PassConfig` +
  AsmPrinter; `llc -march=hcs08` stands up and lowers `ret void` /
  `ret i8 const`. Validates the pipeline.
- **Phase 1 — Correct scalar codegen, Model A (weeks).** i8 / i16 load / store /
  add / sub / and / or / xor / compare / branch / call / ret, all-stack ABI,
  SP-relative frames. Ugly but correct; passes hand-written `llc` FileCheck
  tests.
- **Phase 2 — Direct-page imaginary register file, Model B (weeks-months).**
  The quality investment: ZP register banks, a direct-page allocation pass,
  pseudo expansion, regalloc tuning.
- **Phase 3 — ABI polish + wide / float libcalls + runtime library +
  optimization.** Register-argument CC, `i32` / `i64` / float support, a
  compiler-rt-style HCS08 runtime, peepholes.

## 10. Testing

Per phase: `llc -march=hcs08 -filetype=asm` + FileCheck, growing from
`ret void` up. The **completed MC layer is the encoding oracle** — pipe `llc`
assembly back through `llvm-mc` to catch mismatches. Later: compile tiny C via
clang, and eventually the subset of the LLVM test-suite that fits an 8-bit
target.

## 11. Top risks / open questions

1. **Register allocation with one accumulator** — *the* project risk. Model A
   tolerates it (spills); Model B is the real answer and the hard part.
2. **`CCR` modeling** — must mark `Defs = [CCR]` accurately on ~every
   instruction, or the scheduler / regalloc will reorder across flag clobbers,
   causing silent miscompiles.
3. **`H:X` pressure** — one register serving pointers, 16-bit values, *and* the
   frame base. Expect frequent `pshx` / `pulx` spills.
4. **Direct-page register window** needs a linker / runtime convention (a
   reserved zero-page range) — this cross-cuts into `lld` / startup code.
5. **No ABI / runtime exists** — we define the ABI and must supply a math /
   float runtime library; that is a parallel deliverable, not free.
6. **Big-endian multi-byte correctness** throughout carry chains and frame
   slots.

## 12. Prior art to mine

- **llvm-mos** (6502) — the reference for accumulator-machine LLVM codegen and
  the imaginary-zero-page-register technique. Study before Phase 2.
- **MSP430** (in-tree) — the closest 16-bit skeleton; a direct structural
  template for Phases 0-1.
- **SDCC HC08** and the **CodeWarrior HC08 ABI** — real-world calling
  conventions and code-pattern references.

## 13. What Phase 1 actually looks like

Phase 1 is done: i8 and i16 load / store / add / sub / and / or / xor /
compare / branch / select / call / ret, pointer dereference, i8 <-> i16
conversion, allocas and stack arguments. i32 selects through the generic
expansion. Not done: `mul`, variable shifts, and the runtime library.

Where it diverged from the plan above:

- **The ABI is register-based, not all-stack** (§4 "bring-up"). The first i8
  goes in `A` and the first i16 in `H:X`; the rest go on the stack. The
  register case was no harder and is what most calls hit.

- **`n,sp` addresses SP+n, not SP+1+n** (§5). SP points one byte *below* the
  last thing pushed, so the lowest byte of the frame is `1,sp` and `0,sp` is
  the byte the next push takes. §5 got this wrong and so did the first
  implementation; the frame was one byte low and the bottom slot was
  overwritten by the next call's return address.

- **Second operands live in memory, and getting them there is two problems,
  not one.** There is no reg-reg anything: no ALU, no compare, no 16-bit
  store through a pointer. Each is a pseudo whose operand is parked in a
  frame slot by `EmitInstrWithCustomInserter`, before register allocation has
  to hold two values of a one-register class. Where a carry has to survive
  the sequence - the 16-bit add and sub - the chain itself is emitted in
  `expandPostRAPseudo` instead, after everything that could insert an
  instruction into the middle of it has run.

- **Pseudos are the main structural tool**, more than §7 anticipated, and a
  pseudo that survives to the AsmPrinter used to print as a blank line and
  silently drop its operation. The printer now refuses them.

- **CCR is modelled as one register** (§11.2), so an instruction either
  clobbers all the flags or none. Loads and stores are declared not to touch
  it, which is a lie about N/Z and the truth about C. Nothing exploits the
  lie today: the compare and its branch come out of ISel adjacent, and
  neither of them uses a virtual register, so there is no reason for the
  allocator to insert anything between them. Splitting CCR into NZV and C, as
  llvm-mos does, is the principled fix and would make the carry chains safe
  by construction rather than by inspection.

## Bottom line

Phase 0 -> 1 on Model A gets a *correct* compiler quickly, treating Model B
(the direct-page register file) as the real architecture to migrate to for
usable code. Everything except register allocation is shared between the two
models, so the Model A work is not wasted.
