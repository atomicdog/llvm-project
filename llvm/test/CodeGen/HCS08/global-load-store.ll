; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Phase 1: load and store of a global through 16-bit extended addressing. The
; global reference becomes an R_HCS08_16 relocation at assembly time.

@g8 = global i8 0
@g16 = global i16 0

define i8 @ldg8() {
; CHECK-LABEL: ldg8:
; CHECK:       lda g8
; CHECK-NEXT:  rts
  %v = load i8, ptr @g8
  ret i8 %v
}

define void @stg8() {
; CHECK-LABEL: stg8:
; CHECK:       lda #$07
; CHECK-NEXT:  sta g8
; CHECK-NEXT:  rts
  store i8 7, ptr @g8
  ret void
}

define i16 @ldg16() {
; CHECK-LABEL: ldg16:
; CHECK:       ldhx g16
; CHECK-NEXT:  rts
  %v = load i16, ptr @g16
  ret i16 %v
}

define void @stg16() {
; CHECK-LABEL: stg16:
; CHECK:       ldhx #$1234
; CHECK-NEXT:  sthx g16
; CHECK-NEXT:  rts
  store i16 4660, ptr @g16
  ret void
}
