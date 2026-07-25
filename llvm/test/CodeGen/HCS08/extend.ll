; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Phase 1: changing width between i8 and i16. The two registers are the two
; widths, so these are transfers rather than arithmetic: X is the low half of
; H:X, which makes truncation a single txa and widening a matter of getting
; the right thing into H.

define i8 @truncate(i16 %a) {
; CHECK-LABEL: truncate:
; CHECK:       txa
; CHECK-NEXT:  rts
  %r = trunc i16 %a to i8
  ret i8 %r
}

define i16 @zero_extend(i8 %a) {
; CHECK-LABEL: zero_extend:
; CHECK:       tax
; CHECK-NEXT:  clrh
; CHECK-NEXT:  rts
  %r = zext i8 %a to i16
  ret i16 %r
}

; The sign extension needs a copy of the sign bit in H, and builds it without
; branching: lsla puts the bit in the carry, clra leaves the carry alone, and
; 0 - 0 - carry is 0x00 or 0xff.
define i16 @sign_extend(i8 %a) {
; CHECK-LABEL: sign_extend:
; CHECK:       tax
; CHECK-NEXT:  lsla
; CHECK-NEXT:  clra
; CHECK-NEXT:  sbc #$00
; CHECK-NEXT:  psha
; CHECK-NEXT:  pulh
; CHECK-NEXT:  rts
  %r = sext i8 %a to i16
  ret i16 %r
}

; There is no widening load, so a narrow load is followed by the extension.
define i16 @zero_extend_load(ptr %p) {
; CHECK-LABEL: zero_extend_load:
; CHECK:       lda ,x
; CHECK-NEXT:  tax
; CHECK-NEXT:  clrh
  %v = load i8, ptr %p
  %r = zext i8 %v to i16
  ret i16 %r
}

define i16 @sign_extend_load(ptr %p) {
; CHECK-LABEL: sign_extend_load:
; CHECK:       lda ,x
; CHECK-NEXT:  tax
; CHECK-NEXT:  lsla
  %v = load i8, ptr %p
  %r = sext i8 %v to i16
  ret i16 %r
}

; A truncating store is just a store of the low byte, with no transfer at all.
define void @truncating_store(ptr %p, i16 %v) {
; CHECK-LABEL: truncating_store:
; CHECK-NOT:   txa
; CHECK:       lda ${{[0-9a-f]+}},sp
; CHECK-NEXT:  sta ,x
; CHECK-NEXT:  rts
  %t = trunc i16 %v to i8
  store i8 %t, ptr %p
  ret void
}

; An any-extend takes the zero-extending path rather than leaving H holding
; something nothing is allowed to look at.
define i16 @any_extend(i8 %a, i16 %b) {
; CHECK-LABEL: any_extend:
; CHECK:       tax
; CHECK-NEXT:  clrh
  %z = zext i8 %a to i16
  %r = and i16 %z, %b
  ret i16 %r
}
