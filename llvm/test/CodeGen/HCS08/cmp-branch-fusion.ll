; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; A conditional branch must stay welded to the instruction that set the flags
; it reads. Every reload here is an lda, and lda sets N and Z; register
; allocation inserts reloads without consulting the liveness of a reserved
; register, because LLVM assumes throughout that spill code cannot clobber
; flags. A loop whose carried value is live out of the latch is enough to put
; one at the end of the block - between the compare and the branch - and the
; branch then tests the reload instead of the comparison.
;
; So the two are fused into one instruction until after allocation. What that
; buys is visible below: the reload lands before the compare, not after it.

define zeroext i8 @maximum(ptr nocapture readonly %p, i8 zeroext %n) {
; CHECK-LABEL: maximum:
; The compare and its branch are adjacent, with nothing between them.
; CHECK:       cmp ${{[0-9a-f]+}},sp
; CHECK-NEXT:  b{{[a-z]+}} .LBB
entry:
  %cmp = icmp eq i8 %n, 0
  br i1 %cmp, label %done, label %loop

loop:
  %i = phi i8 [ %i.next, %loop ], [ 0, %entry ]
  %m = phi i8 [ %m.next, %loop ], [ 0, %entry ]
  %idx = zext i8 %i to i16
  %ptr = getelementptr inbounds i8, ptr %p, i16 %idx
  %v = load i8, ptr %ptr
  %gt = icmp ugt i8 %v, %m
  %m.next = select i1 %gt, i8 %v, i8 %m
  %i.next = add i8 %i, 1
  %at.end = icmp eq i8 %i.next, %n
  br i1 %at.end, label %done, label %loop

done:
  %r = phi i8 [ 0, %entry ], [ %m.next, %loop ]
  ret i8 %r
}

; The countdown a variable shift ends with is the same shape: the decrement
; sets the flags and the branch reads them, with no room in between.
define i8 @shift_loop(i8 %a, i8 %b) {
; CHECK-LABEL: shift_loop:
; CHECK:       lsla
; CHECK-NEXT:  dec ${{[0-9a-f]+}},sp
; CHECK-NEXT:  bne
  %r = shl i8 %a, %b
  ret i8 %r
}
