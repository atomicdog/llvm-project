; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Phase 1: 16-bit add/sub of a signed-byte constant to the index register maps
; to a single aix (pointer arithmetic). Subtraction is add of the negated
; constant. General i16 + i16 is not yet supported.

define i16 @add5(i16 %p) {
; CHECK-LABEL: add5:
; CHECK:       aix #$05
; CHECK-NEXT:  rts
  %r = add i16 %p, 5
  ret i16 %r
}

define i16 @dec(i16 %p) {
; CHECK-LABEL: dec:
; CHECK:       aix #$ff
; CHECK-NEXT:  rts
  %r = sub i16 %p, 1
  ret i16 %r
}

define ptr @gep(ptr %p) {
; CHECK-LABEL: gep:
; CHECK:       aix #$04
; CHECK-NEXT:  rts
  %r = getelementptr i8, ptr %p, i16 4
  ret ptr %r
}
