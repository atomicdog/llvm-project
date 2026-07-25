; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Phase 1: arguments that do not fit the two argument registers. CC_HCS08 puts
; the first i8 in A and the first i16 in H:X; everything after that goes in the
; caller's frame, one slot per byte, and is reached with the n,sp forms.
;
; The two sides use different mechanisms to arrive at the same byte. The caller
; writes into the reserved call frame, which PEI places at the bottom of the
; frame, so call-frame offset N is N+1,sp (n,sp addresses SP+n and the lowest
; frame byte is one above SP). The callee reads a fixed frame object sitting
; above the two-byte return address that jsr pushed, at entry-SP + 3 + N.

; A callee with no frame of its own, so the offsets are just the incoming
; ones: b at 3,sp and c at 4,sp.
define i8 @callee_noframe(i8 %a, i8 %b, i8 %c) {
; CHECK-LABEL: callee_noframe:
; CHECK-NOT:   ais
; CHECK:       lda $04,sp
; CHECK-NEXT:  rts
  ret i8 %c
}

; Adding two stack arguments needs no frame either: one is loaded into A and
; the other is read in place by the stack form of the ALU op.
define i8 @callee_leaf(i8 %a, i8 %b, i8 %c) {
; CHECK-LABEL: callee_leaf:
; CHECK-NOT:   ais
; CHECK:       lda $03,sp
; CHECK-NEXT:  add $04,sp
; CHECK-NEXT:  rts
  %t = add i8 %b, %c
  ret i8 %t
}

; The same arguments seen through a frame: every offset moves up by the frame
; size. Here that is the register argument's spill slot plus the byte the
; reg-reg add expansion parks its second operand in.
define i8 @callee_framed(i8 %a, i8 %b, i8 %c) {
; CHECK-LABEL: callee_framed:
; CHECK:       ais #$fd
; CHECK:       lda $06,sp
; CHECK-NEXT:  add $07,sp
  %t = add i8 %b, %c
  %u = add i8 %t, %a
  ret i8 %u
}

; The caller side: two stack bytes reserved by the prologue, written at 1,sp
; and 2,sp so that the return address jsr pushes at 0,sp cannot land on them.
define i8 @caller() {
; CHECK-LABEL: caller:
; CHECK:       ais #$fe
; CHECK:       lda #$03
; CHECK-NEXT:  sta $02,sp
; CHECK:       lda #$14
; CHECK-NEXT:  sta $01,sp
; CHECK:       lda #$64
; CHECK-NEXT:  jsr callee_leaf
; CHECK:       ais #$02
  %r = call i8 @callee_leaf(i8 100, i8 20, i8 3)
  ret i8 %r
}

; A 16-bit argument on the stack, moved with the ldhx/sthx stack forms.
define i16 @callee16(i16 %p, i8 %b, i16 %q) {
; CHECK-LABEL: callee16:
; CHECK:       ldhx $03,sp
  ret i16 %q
}

define i16 @caller16() {
; CHECK-LABEL: caller16:
; CHECK:       ais #$fe
; CHECK:       ldhx #$5678
; CHECK-NEXT:  sthx $01,sp
; CHECK:       ldhx #$1234
; CHECK:       jsr callee16
; CHECK:       ais #$02
  %r = call i16 @callee16(i16 4660, i8 1, i16 22136)
  ret i16 %r
}

; A frame object that is live across a call. The alloca must sit above the
; reserved call frame, so it survives both the outgoing arguments and the
; return address.
define i8 @live_across_call(i8 %x) {
; CHECK-LABEL: live_across_call:
; CHECK:       ais #$fd
; CHECK:       sta $03,sp
; CHECK:       jsr callee_leaf
; CHECK:       add $03,sp
; CHECK:       ais #$03
  %p = alloca i8
  store i8 %x, ptr %p
  %t = call i8 @callee_leaf(i8 1, i8 2, i8 3)
  %s = load i8, ptr %p
  %u = add i8 %s, 100
  ret i8 %u
}
