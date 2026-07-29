//===------------- shifthi3.S - int16 shifts by a variable amount ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// There is no barrel shifter, so a 16-bit shift is a loop over the read-modify-
// write forms: one of them shifts a byte and drops a bit into the carry, and
// the rotate on the other half picks it up. The value therefore never needs to
// be in a register, which is why it goes to a frame word first.
//
// This cannot be written in C, and not only because "a << b" on two ints is
// what the compiler lowers to a call to this function: writing the halves as
// byte shifts would not help either, since a variable byte shift is widened
// and lowered to this same call.
//
// These are only needed on a target whose int is 16 bits, so compiler-rt has
// no generic version of them.
//
// Arguments: the value in H:X and the count in A, which is what the calling
// convention gives for (uint16_t, unsigned char). Result in H:X.
//
// A count of zero has to be tested for rather than let into the loop, because
// dbnza decrements first and would go around 256 times. A count of 16 or more
// is undefined in C and is left to do whatever the loop does with it, which is
// what the inline expansion this replaced also did.
//
//===----------------------------------------------------------------------===//

        .text

// CFI goes to .debug_frame, which has to be asked for. A compiled function
// gets that from the AsmPrinter, which emits .cfi_sections for it; hand-written
// assembly defaults to .eh_frame instead, and .eh_frame is SHF_ALLOC - it would
// be linked into the image and spend flash on a target that has no unwinder to
// read it. .debug_frame is not loaded, so this costs the part nothing.
        .cfi_sections .debug_frame

// Frame: 1,sp = the high byte, 2,sp = the low byte, so the return address is
// at 3,sp. Big endian, and sthx stores H first, so the halves land that way
// round without any rearranging.
//
// The .cfi_ directives are what let a debugger unwind out of these. A compiled
// function gets them from the frame lowering; hand-written assembly has to say
// it itself, and without them no FDE is emitted at all, so a backtrace that
// halts in here cannot find its caller. That is not a remote possibility for
// this file in particular: a variable shift is called from every soft-float
// routine, so it is one of the likeliest places on the target to stop.
//
// The CIE says the CFA is SP+2 on entry (section 24), and each ais moves SP
// without moving the CFA, so each one is followed by the new distance.

        .globl  __ashlhi3
        .type   __ashlhi3,@function
__ashlhi3:
        .cfi_startproc
        ais     #-2
        .cfi_def_cfa_offset 4
        sthx    $01,sp
        tsta
        beq     .Lashl_done
.Lashl_loop:
        lsl     $02,sp                  ; low byte out through the carry...
        rol     $01,sp                  ; ...and into the high byte
        dbnza   .Lashl_loop
.Lashl_done:
        ldhx    $01,sp
        ais     #2
        .cfi_def_cfa_offset 2
        rts
        .cfi_endproc
.Lfunc_end0:
        .size   __ashlhi3, .Lfunc_end0-__ashlhi3

        .globl  __lshrhi3
        .type   __lshrhi3,@function
__lshrhi3:
        .cfi_startproc
        ais     #-2
        .cfi_def_cfa_offset 4
        sthx    $01,sp
        tsta
        beq     .Llshr_done
.Llshr_loop:
        lsr     $01,sp                  ; high byte first, zero shifted in
        ror     $02,sp
        dbnza   .Llshr_loop
.Llshr_done:
        ldhx    $01,sp
        ais     #2
        .cfi_def_cfa_offset 2
        rts
        .cfi_endproc
.Lfunc_end1:
        .size   __lshrhi3, .Lfunc_end1-__lshrhi3

        .globl  __ashrhi3
        .type   __ashrhi3,@function
__ashrhi3:
        .cfi_startproc
        ais     #-2
        .cfi_def_cfa_offset 4
        sthx    $01,sp
        tsta
        beq     .Lashr_done
.Lashr_loop:
        asr     $01,sp                  ; the only difference: the sign is kept
        ror     $02,sp
        dbnza   .Lashr_loop
.Lashr_done:
        ldhx    $01,sp
        ais     #2
        .cfi_def_cfa_offset 2
        rts
        .cfi_endproc
.Lfunc_end2:
        .size   __ashrhi3, .Lfunc_end2-__ashrhi3
