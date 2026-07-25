; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Phase 1: 8-bit ALU on the accumulator. Operations set the condition codes and
; take an immediate or an extended (memory) operand. Commutative operations are
; folded into the memory form.

@g = global i8 0
@g2 = global i8 0

define void @inc() {
; CHECK-LABEL: inc:
; CHECK:       lda #$01
; CHECK-NEXT:  add g
; CHECK-NEXT:  sta g
; CHECK-NEXT:  rts
  %v = load i8, ptr @g
  %r = add i8 %v, 1
  store i8 %r, ptr @g
  ret void
}

define void @xormask() {
; CHECK-LABEL: xormask:
; CHECK:       lda #$0f
; CHECK-NEXT:  eor g
; CHECK-NEXT:  sta g
; CHECK-NEXT:  rts
  %v = load i8, ptr @g
  %r = xor i8 %v, 15
  store i8 %r, ptr @g
  ret void
}

define void @addgg() {
; CHECK-LABEL: addgg:
; CHECK:       lda g
; CHECK-NEXT:  add g2
; CHECK-NEXT:  sta g
; CHECK-NEXT:  rts
  %a = load i8, ptr @g
  %b = load i8, ptr @g2
  %r = add i8 %a, %b
  store i8 %r, ptr @g
  ret void
}
