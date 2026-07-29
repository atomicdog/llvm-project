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

**Superseded in part - read §17 before acting on Model B.** Two claims above
did not survive contact with the evidence. Neither CodeWarrior nor SDCC uses
the zero page as a register file: a CW-built MC9S08AW60 image leaves all 128
bytes of it empty and works entirely from stack frames, and SDCC's whole global
bank is the six bytes of `___SDCC_hc08_ret2..ret7`. And under the decision to
target reentrant code, a general imaginary register file has nothing to hold.
The direct page is still worth taking, but as the much smaller thing §17
describes.

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

- **CCR is two registers, not one** (§11.2 assumed one). `NZV` is written by
  almost everything that touches a value, loads and stores included; `C` only
  by arithmetic that can produce a carry. One register could not express
  both "a load clobbers the flags a branch reads" and "a load does not
  disturb a carry chain", and the 16-bit add needs the second to be true
  while the compare-and-branch needs the first. With them separate, both are
  in the machine IR and the verifier checks them.

## 14. A branch has to be welded to its compare

Saying the truth in the machine IR turns out not to be enough. Register
allocation inserts spill and reload code without consulting the liveness of a
reserved physical register, because LLVM assumes throughout that spill code
does not clobber flags. Here it does: every reload is an `lda`, and `lda` sets
N and Z.

A loop whose carried value is live out of the latch was enough. The reload
went at the end of the block, which is between the compare and the branch, and
the branch tested the reload:

    lda  $03,sp
    cmp  #$01
    lda  $07,sp      <- reload of the loop-carried value
    bne  .LBB0_10    <- tested the reload, not the compare

`while (n--) { if (*p > m) m = *p; p++; }` compiled to exactly that and never
terminated.

Neither instruction could be moved - the compare needs the accumulator for the
value being compared and the reload needs it for something else - so the fix
was to leave no gap. `HCS08FuseCompareBranch` merges the two into one CMPBR
before allocation and `expandPostRAPseudo` splits them again afterwards. The
pseudo is variadic, carrying the branch opcode, the flag-setting opcode and
that instruction's own operands, so one of them covers every addressing mode
of every compare - and the countdown a variable shift ends with, which has the
same shape.

With the operands explicit, the allocator puts the reload *before* the fused
instruction, which is where it belonged.

## 15. The direct page is the user's to spend

Every absolute operand is a byte shorter in the direct-page form, but which
part of `$0000`-`$00FF` a program may use is a property of the board rather
than of the program: the low end is the memory-mapped I/O registers, and how
much RAM follows them differs from part to part. On an MC9S08QY there are 31
registers in page zero and usable RAM starts at `$0060`, leaving about 160
bytes - contended with whatever the user wanted fast. That is a link-time,
part-specific budget, and it is why the compiler does not decide what lives
there. Two addresses are known below `$0100` at compile time, and they are the
only two the direct-page forms are used for:

- **A fixed address.** `*(volatile uint8_t *)0x00C0` is `sta $c0` rather than
  `ldhx #$00c0` / `sta ,x`, which is two bytes more and costs the index
  register. (Above the page it is `sta $nnnn`, still a byte better than going
  through H:X.)
- **A variable the user has placed there**, by putting it in the `.page0`
  section - the statement GCC's m68hc11 port spells `__attribute__((page0))`
  and CodeWarrior spells `__SHORT_SEG`. The linker script decides whether it
  fits, and a `.page0` that lands above `$00FF` is an `R_HCS08_8` overflow at
  link time rather than a silently truncated address.

This is deliberately the same division SDCC and CodeWarrior arrived at, and it
does not prejudge §2: it costs the register allocator nothing and leaves the
page just as available to a future imaginary register file.

`SelectDirectAddr` / `SelectExtendedAddr` are the two `ComplexPattern`s, and
every operation that had an extended form has a direct one beside it: `lda`,
`sta`, `ldhx`, `sthx`, the 8-bit ALU column, `cmp` and `cphx`.

One wrinkle in the assembly syntax. A symbol's value is unknown to the
assembler, so an unadorned one has to be assumed to be anywhere and selects the
extended form - which would quietly undo all of this on the way through a `.s`
file. `<expr` forces the direct-page form and `>expr` the extended one, the
traditional Freescale spelling, and the compiler emits the `<`. That is what
`dpmem` prints and `imm8` does not: a `<` before an immediate already means
something else (`#<expr` is the low byte), and before an `n,sp` displacement it
would mean nothing at all.

## 16. The frame is a byte cheaper through H:X

Every stack-relative form on this machine is on page 2, and the page-2 opcode
byte is the same one the indexed form uses: `lda $03,sp` is `9E E6 03` where
`lda $02,x` is `E6 02`. So a frame access through the index register is the
same instruction without the prefix, and `tsx` - which yields SP+1, hence the
displacement losing one - buys the whole run for a byte.

`HCS08StackToIndexed` does this after `expandPostRAPseudo`, which is what
produces the runs worth rewriting. The one that matters is the 16-bit ALU
chain: six frame accesses between the `sthx` that parks the operand and the
`ldhx` that collects the result, with H:X dead for all of them, because
parking the operand is precisely what freed it.

    ais  #$fe            ais  #$fe
    sthx $01,sp          sthx $01,sp
    lda  $02,sp          tsx
    add  $06,sp          lda  $01,x
    sta  $02,sp    ->    add  $05,x
    lda  $01,sp          sta  $01,x
    adc  $05,sp          lda  $00,x
    sta  $01,sp          adc  $04,x
    ldhx $01,sp          sta  $00,x
    ais  #$02            ldhx $01,sp
    rts                  ais  #$02
                         rts
    29 bytes             24 bytes

Three facts make a run safe to form, and the pass checks or relies on each:

- **H:X must be dead across it.** With one index register that is the binding
  constraint, and it is why a function that returns an `i16` converts nothing
  after the value reaches H:X - `LowerReturn` puts the return register in the
  `rts` operands, so liveness sees it.
- **SP must not move.** `hasReservedCallFrame` is true, so it does not, and the
  pass refuses to cross anything that writes it or has unmodelled side effects
  - which is what turns away the push/pull pairs, since `psha`/`pula` move SP
  without ever naming H:X.
- **`tsx` sets no condition code.** That is what lets a run span a carry chain,
  the thing the 16-bit chain exists to protect.

Worth 47% of all frame accesses and 11% of code size over compiler-rt's 78
sources (186442 -> 166022 bytes). What is left on the table: `ldhx`/`sthx` gain
nothing (the 16-bit indexed forms are themselves page 2, and `sthx` has no
indexed form at all), displacement 1 could use the one-byte `,x` form rather
than `$00,x`, and outgoing-argument stores are not candidates yet.

The idea is not ours: a CodeWarrior-built MC9S08AW60 image reads 480 `tsx`
against 712 remaining plain `n,sp` uses, so it prefers this form roughly two to
one wherever the index register is free.

## 17. The direct-page bank is a spill space, not a register file

§2 planned Model B as llvm-mos's imaginary register file - sixteen to thirty
two imaginary registers in the direct page, with the allocator treating them as
real. Two things since have made that the wrong shape for this machine. The
first is the decision that this compiler targets reentrant code, which puts
parameters and locals on the stack and leaves a general register file nothing
to hold. The second is §16, which took away most of the size argument: `lda
$02,x` is two bytes and so is `lda $50`, so wherever H:X is free the frame is
already as cheap as the page. A bank of general 8-bit locals now buys nothing.

What survives is narrower, and none of it overlaps §16.

**H:X spills, which §16 cannot reach.**

    sthx    dir 2 ($35)   n,sp 3 (9E FF)   n,x  no such form
    ldhx    dir 2 ($55)   n,sp 3 (9E FE)   n,x  3 (9E CE)
    cphx    dir 2 ($75)   n,sp 3 (9E F3)   n,x  no such form

`STHXix` in the `.td` is a pseudo with no opcode because the ISA has no indexed
store of H:X, and the indexed loads are themselves page 2. The `tsx` trick
cannot save H:X, since H:X is what is being saved. So the park-and-reload round
trip between two consecutive 16-bit operations is six bytes on the stack and
four in the page, and the page is the only thing that shortens it. §16 lists
this as left on the table; this is what takes it.

**The operations that exist only in the direct page.** `bset`/`bclr` (2 bytes)
and `brset`/`brclr` (3) have no stack or indexed form at any price, and neither
does `mov` in any of its four. On a frame slot a bit set is `lda`/`ora`/`sta` -
six bytes against two - and a bit test and branch is `lda`/`and`/`beq`, six
against three. Size is not the whole of it: with one accumulator, what these
really save is A, which the load-modify-store sequence destroys. `mov #$01,$50`
is three bytes and leaves A alone where `lda #$01` / `sta $02,x` is four and
does not.

**Scratch that survives H:X being busy.** §16's binding constraint is that H:X
must be dead across the run, so pointer-heavy code - which keeps a pointer
there - gets nothing from it and reverts to `n,sp` and its extra byte. A
direct-page slot is reachable with H:X loaded. `dbnz` and `cbeq` land here too:
three bytes direct against four for `n,sp`, but `dbnz $n,x` is also three, so
they are a byte only while the index register is unavailable.

**What actually lives there.** Not parameters and not locals - reentrancy
settled those onto the stack, and §15 already hands the user the page by
`__attribute__((page0))`. What is left is compiler scratch whose live range
does not cross a call: the H:X park slot, carry-chain temporaries, the
intermediates of an address computation. That restriction is not a limitation
to work around, it is the design. A fixed global address live across a call has
to be saved and restored, and that is four bytes or more, which immediately
eats the two the direct-page form saved. So nothing is allocated across a call,
and what remains is intra-block windows - a spill space of a few bytes, not a
register file of sixteen.

**Where the bank comes from.** The part-specific window size - which §2 left as
the unsolved tension between a link-time budget and compile-time register
classes - is answered the way §15 answers it. `__hcs08_dp_bank` is an undefined
symbol that the linker script places in the page, so a bank the script does not
reserve is an undefined-symbol error and one it puts above `$00FF` is an
`R_HCS08_8` overflow. Neither fails quietly, and the compiler never picks the
address.

## 17a. The contract between the flag and the linker script

Two numbers have to agree and they are set in different files, so the contract
is arranged to make a disagreement fail at link time rather than at run time.

**Asking for it.** `clang -mdirect-page-bank=N`. It reaches the backend as the
function attribute `"hcs08-direct-page-bank"`, one per function, so it survives
inlining and LTO the way a global option would not; `-hcs08-dp-bank-size=N`
overrides it, for `llc` and for tests that have no clang to set an attribute.
The default is none, because the page is the user's (§15) and a compiler that
helped itself to some by default would be taking it from whatever the board
needed it for. Above 256 the driver refuses: a byte past the page has no
direct-page form to be addressed by.

**Reserving it.** The script must place `__hcs08_dp_bank` and give it the same
N. **Anchor it to the top of the page**, which is what makes a disagreement
say so:

    HCS08_DP_BANK_SIZE = 8;
    __hcs08_dp_bank = 0x0100 - HCS08_DP_BANK_SIZE;

    SECTIONS {
      .page0 (NOLOAD) : { *(.page0) *(.page0.*) __page0_end = .; } > page0
      ...
    }
    ASSERT(__page0_end <= __hcs08_dp_bank, "direct page overflowed into the bank")

Byte *k* of the bank is then at `$0100 - N + k`. Every byte the compiler was
promised is inside the page, and the first byte past it is `$0100` - which is
an `R_HCS08_8` overflow naming `__hcs08_dp_bank`, not a silent write over
whatever the script put next. Compiling with 8 against a script that reserved 4
gives

    ld.lld: error: relocation R_HCS08_8 out of range: 256 is not in
    [-128, 255]; references '__hcs08_dp_bank'

Reserving none of it at all is an undefined-symbol error, equally loud. Growing
the bank downwards also keeps the user's `.page0` at fixed addresses, which
matters on a part where those are chosen to sit above the memory-mapped
registers.

**Mixing is safe.** A library built without the bank and a program built with
it link and run together: nothing in the bank is live across a call (§17), so a
callee that uses it cannot disturb a caller that was using it too. The
consequence is only that the saving applies to whatever was compiled with the
flag - a program linked against a stock runtime gets it in its own code and not
in the library's.

## 18. What the bank turned out to be worth

`HCS08DirectPageBank` implements §17, and measuring it moved two of that
section's conclusions.

**It is a rewrite pass, not a register class.** §17 proposed modelling the
slots as caller-saved registers and letting the allocator enforce
never-live-across-a-call for free. That does not work here, because a bank slot
can never be an operand the way a register is: there is no `lda <slot>` where
the slot is a register number, only `lda` with a direct-page *address*, so
every use has to become a different opcode. Promotion is therefore a rewrite,
and the rule has to be checked rather than inherited. The pass runs after
`expandPostRAPseudo`, where a spill slot is only ever `lda`/`sta`/`ldhx`/`sthx`
- the 8-bit ALU column reads the *other* operand of a 16-bit chain, never the
parked one - and walks each block tracking which bytes of the slot the bank
could be standing in for. A call clears that, so does entering a block; a read
of a byte the bank was never given means the value arrived from somewhere the
bank does not reach, and the whole slot stays in the frame. Promoting only the
accesses that pair up would leave one value split between two places.

**Promotion on its own is worth almost nothing, and the reason is §16.** The
pass alone saves 829 bytes of `.text` over 155 compiler-rt sources, 0.50%. §16
already recovers the byte on exactly the accesses the bank would recover it on
- a promoted `lda` saves one byte, and so did the `n,x` form it would have had
- so promoting a run also *shortens* it, and the `tsx` that was amortised over
six accesses is now amortised over two. What is left is `sthx`/`ldhx`, which
§16 cannot touch at all, at two bytes per park: §17 predicted six bytes to four
on the round trip and that is exactly and only what arrives.

**Asking for the slot instead of promoting it is worth ten times as much.**
The temporaries the 16-bit expansions park their operands in are created at
instruction selection, by `CreateSpillStackObject`; taking them from the bank
there instead means no frame object is ever made, and a function whose frame
held nothing else loses its prologue and epilogue with it. `add16` goes 24
bytes to 18, where promotion afterwards could only reach 22 - the `ais` pair
had already been emitted by then. Over the same 155 sources: **12688 bytes,
7.63%**, 78 sources smaller and none larger. Four bytes of bank gets 7.59% of
that, because the first scratch word is where nearly all of it is.

Remeasured 2026-07-26 over the 36 sources the runtime is actually built from,
with eight bytes of bank: **7105 bytes, 9.35%**. The figure moved up because
the variable-shift loops that used to be expanded inline are libcalls now
(§17a is where the flag that switches this on is described), so what is left in
these functions is a higher proportion of the 16-bit chains the bank helps.
The whole simulator matrix - 240 cases - passes with the bank on as well as
off; `f32matrix.py --cflags=-mdirect-page-bank=8` is that run.

These slots need none of the analysis above. Each expansion fills its slot and
consumes it within itself, so no two uses are ever live at once and none
outlives a call - there is no call in any of them. What decides whether one can
move is instead which instructions read it: the direct-page column has no
`lsl`, `ror`, `asr`, `tst`, `dec` or `ldx`, so the variable-shift loop and the
divide keep frame slots of their own. `adc` and `sbc` needed direct forms added
to reach the parked operand of a carry chain.

The two allocators have to agree, since a function can use both: the count of
bank bytes handed out lives in `HCS08MachineFunctionInfo`, instruction
selection takes what it needs first and the pass picks up after it.

**One hazard to write down before interrupts exist.** The bank is one object
shared by the whole program, which is sound only because nothing in it is live
across a call. An interrupt is not a call: a handler that reaches any promoted
code will overwrite the interrupted function's window, and nothing in the
compiler or the linker will say so. This target has no interrupt attribute yet.
When it gets one, a handler has to save and restore the bank - all of it,
since without a call graph it cannot know which bytes its callees use.

## 19. The frame stops at 256 bytes, and it has to say so

Three separate widths bound a frame on this machine, and all three used to be
guarded by an `assert` - which is to say by nothing at all in a release build,
where the value was truncated and codegen carried on.

- **`ais` moves SP by a *signed* byte.** A frame over 127 wrapped. 128 was the
  worst case, because -128 encodes and +128 does not: the epilogue subtracted
  where it meant to add and returned with SP 256 bytes low. Any size works now,
  one `ais` per 128 down or 127 up - two bytes each, and cheaper than the
  `tsx`/`aix`/`txs` round trip, which needs just as many steps for the same
  reason and costs H:X besides.
- **The `n,sp` displacement is one *unsigned* byte**, but instruction selection
  built it as an `i8` node, and the value reaches the MachineOperand through
  `getSExtValue`. So a displacement of 198 arrived as -58 and addressed an
  object 256 bytes away. The constant is `i16` now; the range check on it was
  already right, which is what made this so quiet.
- **Past 255 nothing reaches the object.** `lda` and `sta` have `$nnnn,sp`,
  `ldhx`, `sthx` and `cphx` do not, so the general answer is to compute the
  address into H:X and index from there - which needs a scavenged register and
  is not implemented. This is now a `report_fatal_error` naming the function
  and the offset. Refusing to compile is worse than the fix and much better
  than a silent wrong access.

The last of those is a real ceiling, not a formality: **frame plus incoming
stack arguments together must fit in 256 bytes**. Four compiler-rt sources
exceed it - `__divdc3` at 573, `__divsc3` at 309, `__divdf3` at 302 and
`__muldc3` at 260 - and every one of them was being silently miscompiled
before. All four are soft float or complex arithmetic, neither of which is an
enabled tier, so nothing shipped wrong; but the failure mode was the one that
does not announce itself, and on a part with one or two kilobytes of RAM a
256-byte frame is not far away.

## 20. An interrupt handler saves what the hardware did not

`__attribute__((interrupt))` makes a function usable directly as a service
routine. It must return `void` and take no parameters - Sema says so, and the
backend repeats it as a `report_fatal_error` for hand-written IR, because an
incoming stack argument is addressed past a *two*-byte return address and an
interrupt frame is five, so an argument would be read from the wrong place.

**H is not stacked.** The interrupt sequence pushes the return address, X, A
and the condition codes; H was added to the CPU08 core after that layout was
fixed and never joined it. On this target H:X is the pointer register and the
16-bit accumulator, so nearly every handler clobbers H, and what a lost H
corrupts is the *interrupted* function - intermittently, at a point unrelated
to the bug. So the prologue is `pshh` and the epilogue `pulh`, and the return
is `rti` (`HCS08ISD::RETI_GLUE`, matched onto the existing `RTI`).

This was verified rather than assumed: `swiprobe.s` in the harness sets H:X and
A, executes `swi`, clobbers everything in the handler, and reads back what
survived - `A=0x77` and `X=0x34` restored, `H=0xab` not (2026-07-27).

**The bank is the harder half.** §17 makes the direct-page bank sound by one
argument only: nothing in it is live across a call. An interrupt is not a call.
It can land between the `sthx` that parks an operand and the `ldhx` that
collects it, so a handler reaching any banked code has to put back what it
found. How much it saves has three tiers:

- **No bank, or `no_direct_page_bank`** - nothing. `getHCS08DPBankSize` answers
  zero for both, which is why there is one condition and not two.
- **A leaf** - exactly the bytes it allocated. This is exact only because
  `HCS08DirectPageBank` declines to run on handlers: it runs after PEI, so
  anything it promoted would be restored by a prologue that never counted it.
  Giving up promotion costs the 0.50% of §18 and buys the common ISR - a leaf
  touching no bank - the `pshh`/`pulh` pair and nothing else.
- **A non-leaf** - all of it. The callees are not visible here and may be using
  the bank at the instant the interrupt lands.

`__attribute__((no_direct_page_bank))` is the escape hatch, and it is worth
being precise about what kind. The transitive half - that nothing the handler
*calls* touches the bank - is the user's word and uncheckable. The local half
is not merely trusted: `getHCS08DPBankSize` reports zero for such a function, so
ISel and the promotion pass have no way to hand it a slot, and the corrupting
combination (uses the bank, does not save it) cannot be built through the
attributes at all. The negative control in `isrtest.py` has to be written in
assembly for exactly that reason.

**Ordering is load-bearing.** The prologue is `pshh`, then the bank pushes, then
`ais -frame`; the epilogue reverses it. `n,sp` displacements are measured from
where SP finally lands, so a push *after* the `ais` would move SP out from under
every one of them.

**Vectors are the linker script's job.** The attribute only changes code
generation. MC9S08 parts do not agree on a vector map, so the compiler does not
pick one:

    .vectors : { SHORT(tpm_overflow); ... SHORT(_start); } > vectors

The handler is never referenced from code, so it needs `KEEP` or `used` to
survive section GC.

**The contract with the rest of the program**, alongside §17a's contract with
the linker script: a handler must be compiled with an `N` at least as large as
anything else in the program, or carry `no_direct_page_bank`. A handler built
with no bank while its callees have one saves nothing and corrupts them. That
one is not checkable here - `N` is a link-time, whole-program quantity and this
is a per-function decision.

Left undone deliberately: a *non-leaf* handler could save less than all of `N`
if the save moved into a post-PEI pass that had the call graph. Nothing needs
it yet.

## 21. The instructions with no expression in C

Seven builtins, for the instructions that act on the CPU instead of on a value.
Each is a `ClangBuiltin<>` on the intrinsic in `IntrinsicsHCS08.td`, which is
the whole mapping: the generic path in CGBuiltin looks the name up under the
target's arch prefix, so there is no HCS08 case in `CGBuiltin.cpp` and no
`TargetBuiltins/HCS08.cpp`.

| builtin | instruction |
|---|---|
| `__builtin_hcs08_sei()` / `_cli()` | `sei` / `cli` |
| `__builtin_hcs08_get_ccr()` / `_set_ccr(u8)` | `tpa` / `tap` |
| `__builtin_hcs08_wait()` / `_stop()` | `wait` / `stop` |
| `__builtin_hcs08_nop()` | `nop` |

**The sense of `sei`/`cli` is the opposite of the guess.** I is a *mask*, so
`sei` disables interrupts and `cli` enables them. Verified on ucsim rather than
argued from the mnemonic (`intrtest.c`): `cli` then `get_ccr & 0x08` reads 0,
`sei` reads 8.

**They are memory barriers, deliberately.** The intrinsics are
`IntrHasSideEffects` *without* `IntrNoMem`, so LLVM must assume they read and
write memory. With `IntrNoMem` a load or store could be hoisted out of the
region the two delimit, which is the one thing a critical section exists to
prevent. The `barrier` case in `intrinsics.ll` pins this down on non-volatile
memory, where nothing else would stop the motion.

**`sei`/`cli` alone do not nest.** A function that ends its critical section
with `cli` unmasks even when its caller had masked. The composable form is the
register:

    unsigned char s = __builtin_hcs08_get_ccr();
    __builtin_hcs08_sei();
    ... critical section ...
    __builtin_hcs08_set_ccr(s);

Only the control bits round-trip. The condition codes in a `get_ccr` result are
whatever the last flag-setting instruction left, because ordinary arithmetic may
be scheduled either side of it - so do not read carry out of one and expect it
back from the other. `tpa` accordingly does *not* declare `NZV`/`C` as uses:
nothing defines them at an arbitrary point, so it would be a use of an undefined
physical register, and the value genuinely is unspecified. `tap` does declare
both as defs, which is what stops a branch being scheduled across it and testing
the restored bits instead of its own compare.

Two smaller notes. `nop` is the one instruction here that the MC layer declares
`hasSideEffects = 0`, so it is selected to a code-generation-only view that says
otherwise - matched directly, `DeadMachineInstructionElim` would delete the only
thing the caller asked for. And `stop` is disabled out of reset on most MC9S08
parts, where enabling it is a write to SOPT; on a part that has not done so it
behaves as an illegal opcode. That is the program's business, not the
compiler's.

## 22. `double` is 32 bits

`double` and `long double` are both the same type as `float`. This is an ABI
decision and it is the user's, taken 2026-07-27.

**64-bit `double` was never a working configuration**, so leaving it at 64 bits
bought nothing to be conformant with. `__divdf3` alone wants a 302-byte frame
against the 256-byte ceiling of §19 and does not compile at any setting; even
if it did, `muldf3` is 14KB and `divdf3` 19KB against a part with 32KB of
flash. What the wide `double` actually produced was a link error against
`__adddf3` the first time anyone wrote a floating-point constant without an `f`
suffix.

Two things it also fixes:

- **`printf("%f")` is possible now.** C's default argument promotions turn a
  `float` passed through `...` into a `double`; while that was 64 bits the call
  did not link, wanting `__extendsfdf2` and `__adddf3`. With the two the same
  width the promotion is the identity. `vafloat.c` checks this end to end,
  including the case that catches a wrong stride - an `int` read *after* a
  float in the same list.
- **`double` arithmetic reaches the runtime that exists.** It is emitted as
  `float`, so it lands in `__addsf3` and friends, which is the half of
  compiler-rt this target actually builds.

This is what SDCC does for these parts, and what avr-gcc spells
`-fshort-double`; clang's AVR target does the same unconditionally.
`-mdouble=` stays AVR-only in the driver, so asking for 64 bits here is a clean
*unsupported option for target* rather than a distant undefined symbol - which
is the right answer, since there is no way to make it work.

**`__SOFTFP__` matters more than before.** `fixsfdi.c` and `fixunssfdi.c` each
carry two implementations, and the one chosen without it converts through
`double` to get at a 64-bit intermediate. That used to fail loudly, wanting
`__muldf3`; now `double` *is* `float`, so it would quietly compute in 24 bits of
mantissa and return a wrong answer. The define is set by the clang target and
those two routines are the reason.

## 23. Debug info

`-g` used to abort the compiler. `SupportsDebugInformation` was set from the
start, but nothing behind it was: three separate things were missing, and each
hid the next.

**A 32-bit relocation.** The relocation set stopped at 16 bits, which is every
width this machine can address. 32-bit DWARF refers between its own sections
with four-byte offsets whatever the target's pointer size, so `FK_Data_4` fell
through to the ELF writer's `llvm_unreachable` and `-g` died there.
`R_HCS08_32` is deliberately not range-checked against the 16-bit address space
the other relocations are - it is not an address. Three places had to learn it:
the object writer, lld, and `RelocationResolver.cpp`, which is what lets
`llvm-dwarfdump` read an unlinked `.o` (without it every string came back
empty and the DWARF looked malformed).

**DWARF register numbers.** The target defined none. There is no published
psABI for this family and so no published DWARF numbering either, which makes
this ours to define, exactly as the relocation set is:

    0  A      1  H       2  X       3  H:X
    4  SP     5  PC      6  NZV     7  C

`H:X` gets a number as well as its halves because it is the only allocatable
16-bit register, so it is where a pointer or an `int` actually lives. Without
these, a variable's location came out as a bare `DW_OP_plus_uconst` with
nothing to be relative to, and no subprogram had a `DW_AT_frame_base` at all.

**The frame reference was off by one.** This is the one that would have been
lived with. `getFrameIndexReference` is what debug info asks where a local
sits, and the inherited default answers object offset plus frame size -
while `eliminateFrameIndex`, which is what the *code* uses, adds one more,
because SP points one byte below the last thing pushed so the lowest byte of
the frame is `1,sp`. Every local was therefore described one byte low. That
does not fail: a debugger reads the high half of one variable joined to the low
half of its neighbour and prints a plausible wrong number. The override exists
to keep the two formulas identical and says so.

**What works now**: line tables with `is_stmt` and `prologue_end`, so an
address maps to a source line - which is the immediately useful thing, since a
BDM probe reports PC on halt. Variable locations resolve to real frame slots.
`llvm-dwarfdump --verify` is clean on both the object and the linked image.

`dbgtest.py` in the harness is the test that matters: it reads out of the DWARF
where `local` is supposed to be, halts there on the simulator, and checks the
value is actually at that address. `--verify` only says the DWARF is well
formed; this says it is true.

CFI came next and is §24.

### A use-after-free the debug work uncovered

`-g` also made every mnemonic in a hand-written `.s` file fail to match -
`ldhx`, `txs`, `rts` alike, operands or not. That was not a debug bug.
`parseInstruction` lower-cased the mnemonic into a local `std::string` and
handed a `StringRef` to it to `HCS08Operand`, which does not own it; the
matcher does not run until `matchAndEmitInstruction`, after that local has
died. It read freed stack that usually still held the right bytes, and `-g`
does enough work in between to disturb them. It is `MCContext::allocateString`
now, whose storage lasts as long as the parse. Every `.s` file the assembler
has ever handled was relying on luck.

## 24. CFI, and where to put the CFA

§23 left a debugger able to say where it was and what the locals were, but not
who called it. CFI closes that: `.debug_frame` now describes every instruction
boundary in every function.

### The CFA is the caller's SP, and that decision is the whole section

Everything else follows from where the CFA is put, and this machine makes the
choice sharper than most, because **SP points one byte *below* the last thing
pushed**. Two definitions are available:

- **The caller's lowest occupied byte** - the byte its own `1,sp` named. Reads
  beautifully: the return address lands at `CFA-2`, the first incoming stack
  argument at `CFA+0`, and it lines up with the `+1` in `getFrameIndexReference`.
- **The caller's SP at the call site**, which is what DWARF itself calls the
  typical definition. Reads slightly oddly here, as below.

The first is wrong, and it is worth being precise about why, because it is
wrong for a reason that no test of a single frame would ever show. An unwinder
walks by evaluating the *caller's* rule, and that rule is written against SP -
so it must first recover the caller's SP. Nothing in the FDE tells it how:
libunwind assigns the CFA to SP unconditionally
(`DwarfInstructions.hpp`, `newRegisters.setSP(cfa)`), and gdb's ports default
the SP rule to the CFA. Under the first definition the caller's SP is `CFA-1`,
which would need an explicit `DW_CFA_val_offset` that neither consumer consults
- and ignoring it walks every parent frame off by one, **silently and
cumulatively**. One frame would look right. Three would not.

So the CFA is the caller's SP, and the oddity is absorbed on the other side:

    DW_CFA_def_cfa: SP +2
    DW_CFA_offset:  PC -1

**The return address straddles the CFA rather than sitting below it.** `jsr`
stores PCL at the CFA itself and PCH one below, so the two bytes read
high-first begin at `CFA-1`. That is not a mistake to be tidied up later; it is
the direct consequence of SP pointing below the stack top, and the `-2` that
the same reasoning gives on every ordinary machine would be the error here.

### What each kind of function says

An ordinary function only moves SP, so it only restates the offset - once per
`ais`, not once per prologue. A frame over 127 bytes takes several `ais`, and
describing each keeps the rule true at every boundary, which is the entire
point when a BDM probe halts wherever it halts.

An interrupt handler is entered with five bytes already pushed where a `jsr`
pushes two, so it restates the distance and describes what the hardware
stacked. Counting back, the byte pushed when the distance became `N` sits at
`CFA-(N-1)`:

    CFA-0  PCL          CFA-3  A
    CFA-1  PCH          CFA-4  condition codes
    CFA-2  X            CFA-5  H, from the handler's own pshh

**These offsets are measured, not read off the manual.** `frameprobe.py` builds
`swiframe.s`, which `tsx`es inside the handler and reads its own stacked frame
back. This is the same discipline §20 used for "H is not stacked": a push order
is exactly the kind of claim that reads correctly and is backwards.

Two things have no rule and should not have one. The **condition codes** at
`CFA-4`, because LLVM models the CCR as the two halves `NZV` and `C` (§6) and
neither names the whole register - and nothing unwinds through the flags. And
the **direct-page bank bytes** a non-leaf handler pushes, because they are
memory rather than a register and DWARF has no way to say a memory location was
saved; only the CFA moves for them. Putting them back is the epilogue's job and
no unwinder's.

### The epilogue is described too, and that forces one more thing

Most targets skip epilogue CFI. Here it is worth having - `.debug_frame` is not
an allocated section, so accuracy costs nothing on a 32KB part, and halting at
an arbitrary PC is the reason this target has debug info at all.

But describing epilogues creates a problem that prologue-only CFI does not
have. A function with an early return has **two** epilogues, and the block laid
out after the first would inherit its rules while the frame is still up - wrong
for that block's whole length, not for an instruction or two. That is what
`setCFIFixup(true)` is for: the generic pass brackets the range with
`.cfi_remember_state`/`.cfi_restore_state`. `resetCFIToInitialState` is
implemented alongside it; the pass only calls it for a block reachable without
the prologue, which does not arise while PEI puts the prologue in the entry
block, but the default is a silent no-op and a wrong answer if it ever does.

### Settings that are load-bearing

- `UsesCFIWithoutEH = true` with `ExceptionsType = None`. These are separate
  questions: there is no unwinder here and nothing to throw with, but a
  debugger needs the same description an unwinder would. The pair is what asks
  for `.debug_frame` without `.eh_frame` coming with it. Without `-g` nothing
  is emitted at all - no directives, no sections.
- `CalleeSaveStackSlotSize = 1`, which despite its name feeds exactly one
  thing: the DWARF **data alignment factor**. Every push here is one byte
  (`pshh`, `psha`, `pshx`; there is no 16-bit push), the stack is byte-aligned,
  and the interrupt frame is an odd five bytes - so the old `2` would have left
  half the saved registers unable to use the compact `DW_CFA_offset` form and
  described none of them better.
- DWARF register numbers, from §23, are what the rules are written in terms of;
  the RA column is `PC`, which is 5.

### The test that matters

`cfitest.py` halts three frames deep and unwinds using nothing but
`.debug_frame`: read the row for the PC, compute `CFA = SP + offset`, take the
return address from `CFA-1`, step with `SP = CFA`, repeat. Each address has to
land inside the function that is really there.

Three frames, not two, and deliberately: **one frame can come out right from a
wrong rule that happens to cancel, and the off-by-one above is exactly such a
rule.** `middle` carries an array so it has a frame of its own, so a wrong
frame size lands the second step somewhere else. `--verify` cannot catch any of
this - it only says the DWARF is well formed - and reading the unwind rows back
only proves they are self-consistent.

### Confirmed on silicon

`hwcfitest.py` asks the same question of a physical MC9S08AW60 over BDM, and it
can ask one thing the simulator cannot. A breakpoint stops at an address
somebody chose; a BDM halt interrupts a running CPU wherever it happens to be,
including part-way through a prologue - which is the case the per-`ais`
description exists for and the case no chosen breakpoint would think to probe.

So `hwcfi.c` is shaped to make that likely rather than hoped for: `tiny` has a
200-byte frame, which is over the 127 an `ais` can step and so takes *two* of
them at each end, and almost no body, while `leaf` calls it in a loop whose
body is nothing but the call. **30 halts, all 30 walking four frames correctly,
5 of them with the frame only partly built** - four at the function's first
instruction, where the CFA is still `SP+2` against the body's `SP+204`, and one
at `PC=0x2012`, *between* the two prologue `ais` with 130 of the 204 bytes
allocated. That last one is the whole argument for describing each `ais`
separately, observed rather than reasoned about.

Getting there took two corrections worth keeping. The frame has to be made to
survive optimisation: marking the array `volatile` is not enough, because SROA
may split an alloca whose accesses are all at constant offsets, volatile or
not, and it did - two adjacent scalars and a four-byte frame. Letting the
address escape to a global is what keeps the 200 bytes. And the expected
backtrace depends on where the halt landed, since an asynchronous stop in
`leaf` means three frames are live and not four; comparing against a fixed
depth walks off the top of the stack past `run` and reports a failure that is
the test's, not the compiler's.

**Not done**: there is still no `.eh_frame`, which is correct, since there is
no unwinder to read it.

## 25. The bank and the debug info, which did not know about each other

Two bugs, found by auditing what `-g` actually produced rather than by anything
failing. Both come from the same place: `HCS08DirectPageBank` runs at
`addPreEmitPass`, which is *after* LiveDebugValues has already written down
where every value lives.

**The bank left variable locations pointing at addresses nothing writes.** It
promotes a spill slot to page 0 and rewrites the `n,sp` accesses, but the
`DBG_VALUE`s naming that slot were untouched, so the location list went on
saying `DW_OP_breg4 SP+N` for a slot the function no longer uses. Demonstrated
with `-O2 -g -mdirect-page-bank=8`: named locals claimed `SP+11` and `SP+13`
while the assembly contained no `$0b,sp`, no `$0d,sp`, no `tsx` and no `,x`
access at all. `llvm-dwarfdump --verify` passes it, because the DWARF is well
formed and merely untrue - the §23 failure mode again, a plausible wrong number
rather than a crash.

The fix is to drop those locations, not to keep them. The value now lives at
`__hcs08_dp_bank` plus an offset, which is a **link-time** address, and a debug
value can carry a register, a constant or a target index but not a relocatable
symbol - `DbgValueLocEntry` has no kind for one. So the variable reads as
optimized out for that range, which it usually has company for: the same
variable is typically live in H:X over neighbouring ranges and those are
unaffected.

The tempting alternative is worse. Declining to promote a slot that a
`DBG_VALUE` names would give better debug info and would make `-g` compile to
different instructions than `-O2` alone. **That is not a trade any pass may
make**, which is also the whole of the second bug:

**`-g` was already changing which slots got promoted.** `gatherSlot` walked
every instruction in the block, and a `DBG_VALUE $sp, 0, ...` presents a
register followed by an immediate - exactly the shape `findSPDispOperand`
matches - so it read as an `n,sp` access at displacement 0 and disqualified
whatever slot sat at the bottom of the frame. Skipping debug instructions
before anything else looks at them is the fix, and skipping rather than
resetting the run, since describing a value does not end one.

That one is worth a note on how it was confirmed. It does not reproduce on a
small synthetic function - two hand-written cases compiled identically with and
without `-g`, which looked like evidence that the hazard was theoretical. It
took a real corpus: **4 of 12 compiler-rt sources produced different
instructions under `-g`** before the fix, and 0 of 30 configurations after.
Reasoning about a mechanism is not the same as showing it fires, and a test too
small to hit it says nothing either way.

`dp-bank-debug-values.mir` covers both directions, which is the part that
matters: a promoted slot's debug value is dropped, and a slot the bank declined
- read without being stored first, so its value came from somewhere the bank
cannot have held it - keeps its debug value untouched. Dropping every debug
value that mentions the frame would be safe and useless.

Registering the machine passes is what made that test possible. All three were
declared and none were registered, so `-run-pass` and `-stop-before` could not
reach them and the only way to exercise one was to compile a whole function and
read the assembly.

## Bottom line

Phase 0 -> 1 on Model A gets a *correct* compiler quickly. Model B as §2
imagined it - a general imaginary register file - is superseded by §17: what
the direct page is worth on this machine is a small call-free spill space
beside the stack, not a bank instead of it. Everything except register
allocation was shared between the two models either way, so the Model A work is
not wasted.
