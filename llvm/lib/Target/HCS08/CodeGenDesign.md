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

## Bottom line

Phase 0 -> 1 on Model A gets a *correct* compiler quickly. Model B as §2
imagined it - a general imaginary register file - is superseded by §17: what
the direct page is worth on this machine is a small call-free spill space
beside the stack, not a bank instead of it. Everything except register
allocation was shared between the two models either way, so the Model A work is
not wasted.
