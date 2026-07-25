; RUN: llvm-mc -triple=hcs08 -motorola-integers -filetype=obj %s -o %t.o
; RUN: llvm-readobj -r -x .text %t.o | FileCheck %s

; An in-range branch to a symbol defined in the same section resolves at
; assembly time to a signed displacement measured from the end of the
; instruction, and leaves no relocation behind.
start:
	bra	fwd
	nop
fwd:
	bra	start

; bra fwd sits at 0x0 and ends at 0x2; fwd is at 0x3, so the displacement is
; +1. The nop is 0x9d. bra start sits at 0x3 and ends at 0x5; start is at 0x0,
; so the displacement is -5 = 0xfb.
; CHECK:      Relocations [
; CHECK-NEXT: ]
; CHECK: 0x00000000 20019d20 fb
