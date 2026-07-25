; RUN: llc -mtriple=hcs08 < %s | FileCheck %s

; Phase 1: 8-bit unary operations on the accumulator.

define i8 @neg(i8 %x) {
; CHECK-LABEL: neg:
; CHECK:       nega
; CHECK-NEXT:  rts
  %r = sub i8 0, %x
  ret i8 %r
}

define i8 @not(i8 %x) {
; CHECK-LABEL: not:
; CHECK:       eor #$ff
; CHECK-NEXT:  rts
  %r = xor i8 %x, -1
  ret i8 %r
}
