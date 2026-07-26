//===------- udivmodhi4.S - unsigned int16 division and remainder ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The hardware divide is 16/8 -> 8, which cannot do a 16-bit divisor, so this
// is the usual restoring shift-and-subtract loop. It cannot be written in C:
// "a / b" on two unsigned ints is what the compiler lowers to a call here.
//
// The dividend is shifted left out of one end and the quotient shifted in at
// the other, so the two share a word. Sixteen iterations:
//
//   rem = (rem << 1) | (num >> 15);  num <<= 1
//   if (rem >= den) { rem -= den; num |= 1 }
//
// Arguments: a in H:X, b at 3,sp (high) and 4,sp (low). Result in H:X.
// Division by zero is undefined in C and not checked for here.
//
//===----------------------------------------------------------------------===//

        .text

// Frame: 1,sp/2,sp = dividend, becoming the quotient
//        3,sp/4,sp = remainder
//        5,sp      = iteration count
// so the return address is at 6,sp and b at 8,sp / 9,sp.
.macro  udivmodhi4_body
        ais     #-5
        sthx    $01,sp
        clra
        sta     $03,sp
        sta     $04,sp                  ; remainder starts empty
        lda     #16
        sta     $05,sp
.Lloop\@:
        lsl     $02,sp                  ; shift the dividend left...
        rol     $01,sp
        rol     $04,sp                  ; ...and its top bit into the remainder
        rol     $03,sp
        // A bit carried out of the remainder means it is a 17-bit value, which
        // is necessarily larger than the divisor: no comparison needed.
        bcs     .Ltake\@
        ldhx    $03,sp
        cphx    $08,sp
        bcs     .Lnext\@                ; borrow, so remainder < divisor
.Ltake\@:
        lda     $04,sp
        sub     $09,sp
        sta     $04,sp
        lda     $03,sp
        sbc     $08,sp
        sta     $03,sp
        lda     $02,sp                  ; and record the quotient bit
        ora     #$01
        sta     $02,sp
.Lnext\@:
        dbnz    $05,sp,.Lloop\@
.endm

        .globl  __udivhi3
        .type   __udivhi3,@function
__udivhi3:
        udivmodhi4_body
        ldhx    $01,sp
        ais     #5
        rts
.Lend_udiv:
        .size   __udivhi3, .Lend_udiv-__udivhi3

        .globl  __umodhi3
        .type   __umodhi3,@function
__umodhi3:
        udivmodhi4_body
        ldhx    $03,sp
        ais     #5
        rts
.Lend_umod:
        .size   __umodhi3, .Lend_umod-__umodhi3
