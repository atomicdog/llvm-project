; RUN: llc -mtriple=hcs08 < %s | FileCheck %s

; Phase 1: integer compare and conditional branch. The compare sets the
; condition codes and the branch reads them; branch analysis lets the taken
; block fall through.

@g = global i8 0

define void @eq5() {
; CHECK-LABEL: eq5:
; CHECK:       lda g
; CHECK-NEXT:  cmp #$05
; CHECK-NEXT:  bne .LBB0_2
; CHECK:       lda #$01
; CHECK:       .LBB0_2:
; CHECK:       lda #$02
  %v = load i8, ptr @g
  %c = icmp eq i8 %v, 5
  br i1 %c, label %then, label %else
then:
  store i8 1, ptr @g
  ret void
else:
  store i8 2, ptr @g
  ret void
}

; A signed less-than is canonicalized to a "greater than" test of the inverse.
define void @slt() {
; CHECK-LABEL: slt:
; CHECK:       lda g
; CHECK-NEXT:  cmp #$09
; CHECK-NEXT:  bgt .LBB1_2
  %v = load i8, ptr @g
  %c = icmp slt i8 %v, 10
  br i1 %c, label %lo, label %hi
lo:
  store i8 0, ptr @g
  ret void
hi:
  store i8 1, ptr @g
  ret void
}
