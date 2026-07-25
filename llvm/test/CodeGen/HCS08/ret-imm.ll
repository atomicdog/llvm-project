; RUN: llc -mtriple=hcs08 < %s | FileCheck %s

; Phase 1: materialize an integer constant into the return register and return.
; An i8 result goes in the accumulator A, an i16 result in the index pair H:X.

define i8 @r8() {
; CHECK-LABEL: r8:
; CHECK:       lda #$2a
; CHECK-NEXT:  rts
  ret i8 42
}

define i16 @r16() {
; CHECK-LABEL: r16:
; CHECK:       ldhx #$1234
; CHECK-NEXT:  rts
  ret i16 4660
}
