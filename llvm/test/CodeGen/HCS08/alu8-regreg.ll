; RUN: llc -mtriple=hcs08 < %s | FileCheck %s

; Phase 1: register/register ALU. HCS08 has no reg-reg arithmetic and A is the
; only allocatable 8-bit register, so the second operand always has to come
; from memory.
;
; Where it already is in memory it is read in place. Where it is genuinely in
; a register the pseudo's custom inserter parks it in a stack byte first,
; before register allocation would otherwise have to hold two 8-bit values at
; once. One such byte serves the whole function: each expansion writes it and
; reads it back immediately, so no two uses overlap.

@g = global i8 0
@g2 = global i8 0
@r1 = global i8 0
@r2 = global i8 0

; Both %a and %b are used twice, so neither load can fold into an operation as
; a single-use global load, and both subtractions go through the parked byte -
; the same one, $03,sp.
define void @subs() {
; CHECK-LABEL: subs:
; CHECK:       ais #$fd
; CHECK:       sta $03,sp
; CHECK-NEXT:  lda ${{[0-9a-f]+}},sp
; CHECK-NEXT:  sub $03,sp
; CHECK:       sta $03,sp
; CHECK-NEXT:  lda ${{[0-9a-f]+}},sp
; CHECK-NEXT:  sub $03,sp
; CHECK:       ais #$03
  %a = load i8, ptr @g
  %b = load i8, ptr @g2
  %s = sub i8 %a, %b
  %t = sub i8 %b, %a
  store i8 %s, ptr @r1
  store i8 %t, ptr @r2
  ret void
}

; A single-use global load folds straight into the extended form instead - no
; frame at all.
define i8 @fold_global(i8 %a) {
; CHECK-LABEL: fold_global:
; CHECK-NOT:   ais
; CHECK:       and g
; CHECK-NEXT:  rts
  %b = load i8, ptr @g
  %r = and i8 %a, %b
  ret i8 %r
}

; So does a frame object, through the stack form: an argument passed on the
; stack is read where it lies, with no frame of our own.
define i8 @fold_frame(i8 %a, i8 %b, i8 %c) {
; CHECK-LABEL: fold_frame:
; CHECK-NOT:   ais
; CHECK:       lda $03,sp
; CHECK-NEXT:  ora $04,sp
; CHECK-NEXT:  rts
  %r = or i8 %b, %c
  ret i8 %r
}
