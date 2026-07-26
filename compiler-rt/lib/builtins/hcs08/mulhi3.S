//===------------- mulhi3.S - int16 multiplication ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The hardware multiply is 8x8 -> 16 (X:A <- A * X), so a 16-bit product is
// three of them. This cannot be written in C: "a * b" on two ints is what the
// compiler lowers to a call to this function.
//
//   a * b = al*bl + ((ah*bl + al*bh) << 8)   (mod 2^16)
//
// The ah*bh term contributes only above bit 15 and is dropped.
//
// Arguments: a in H:X, b at 3,sp (high) and 4,sp (low). Result in H:X.
//
//===----------------------------------------------------------------------===//

        .text
        .globl  __mulhi3
        .type   __mulhi3,@function

// Frame: 1,sp = ah   2,sp = al   3,sp = result high   4,sp = result low
// so the return address is at 5,sp and b at 7,sp / 8,sp.
__mulhi3:
        ais     #-4
        sthx    $01,sp                  ; split a into its two bytes

        lda     $02,sp                  ; al
        ldx     $08,sp                  ; bl
        mul                             ; X:A = al * bl
        sta     $04,sp                  ; the whole low half of the result
        stx     $03,sp                  ; and the carry into the high half

        lda     $01,sp                  ; ah
        ldx     $08,sp                  ; bl
        mul                             ; only the low byte matters
        add     $03,sp
        sta     $03,sp

        lda     $02,sp                  ; al
        ldx     $07,sp                  ; bh
        mul
        add     $03,sp
        sta     $03,sp

        ldhx    $03,sp
        ais     #4
        rts
.Lfunc_end0:
        .size   __mulhi3, .Lfunc_end0-__mulhi3
