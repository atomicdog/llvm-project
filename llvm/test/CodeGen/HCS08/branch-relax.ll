; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Every relative branch here reaches a signed byte from the end of the
; instruction, which no function of any size stays inside: each operation is
; several three-byte instructions. Out-of-range branches become jmp, which
; takes a 16-bit absolute address and so reaches anywhere.
;
; The distance is measured by adding up instruction sizes, so this also covers
; getInstSizeInBytes being implemented - its default answer is "unknown", which
; makes every branch look to be in range and leaves the assembler to reject
; them.

@g = global i16 0

; The branch back to the top of the loop has to span the whole body.
define void @far_branch(i16 %n) {
; CHECK-LABEL: far_branch:
; CHECK:       jmp .LBB0_
entry:
  br label %loop

loop:
  %i = phi i16 [ 0, %entry ], [ %next, %loop ]
  store volatile i16 1, ptr @g
  store volatile i16 2, ptr @g
  store volatile i16 3, ptr @g
  store volatile i16 4, ptr @g
  store volatile i16 5, ptr @g
  store volatile i16 6, ptr @g
  store volatile i16 7, ptr @g
  store volatile i16 8, ptr @g
  store volatile i16 9, ptr @g
  store volatile i16 10, ptr @g
  store volatile i16 11, ptr @g
  store volatile i16 12, ptr @g
  store volatile i16 13, ptr @g
  store volatile i16 14, ptr @g
  store volatile i16 15, ptr @g
  store volatile i16 16, ptr @g
  store volatile i16 17, ptr @g
  store volatile i16 18, ptr @g
  store volatile i16 19, ptr @g
  store volatile i16 20, ptr @g
  store volatile i16 21, ptr @g
  store volatile i16 22, ptr @g
  store volatile i16 23, ptr @g
  store volatile i16 24, ptr @g
  store volatile i16 25, ptr @g
  store volatile i16 26, ptr @g
  store volatile i16 27, ptr @g
  store volatile i16 28, ptr @g
  store volatile i16 29, ptr @g
  store volatile i16 30, ptr @g
  %next = add i16 %i, 1
  %c = icmp eq i16 %next, %n
  br i1 %c, label %out, label %loop

out:
  ret void
}

; A short branch is left alone.
define void @near_branch(i16 %n) {
; CHECK-LABEL: near_branch:
; CHECK-NOT:   jmp
; CHECK:       rts
entry:
  %c = icmp eq i16 %n, 0
  br i1 %c, label %t, label %out
t:
  store volatile i16 1, ptr @g
  br label %out
out:
  ret void
}
