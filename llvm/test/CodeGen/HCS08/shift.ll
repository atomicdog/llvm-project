; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Phase 1: the 8-bit ALU shifts one bit at a time, so a constant shift unrolls
; to that many single-bit accumulator shifts.

define i8 @shl3(i8 %x) {
; CHECK-LABEL: shl3:
; CHECK:       lsla
; CHECK-NEXT:  lsla
; CHECK-NEXT:  lsla
; CHECK-NEXT:  rts
  %r = shl i8 %x, 3
  ret i8 %r
}

define i8 @lshr2(i8 %x) {
; CHECK-LABEL: lshr2:
; CHECK:       lsra
; CHECK-NEXT:  lsra
; CHECK-NEXT:  rts
  %r = lshr i8 %x, 2
  ret i8 %r
}

define i8 @ashr1(i8 %x) {
; CHECK-LABEL: ashr1:
; CHECK:       asra
; CHECK-NEXT:  rts
  %r = ashr i8 %x, 1
  ret i8 %r
}
