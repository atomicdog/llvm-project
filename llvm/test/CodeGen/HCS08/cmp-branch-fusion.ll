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

; A countdown loop is the same shape from the other direction: the value
; carried around the latch is live out of it, so allocation wants to put a
; reload at the end of the block, and the test of the counter has to keep its
; branch. This used to be written as a variable shift, which expanded to
; exactly such a loop; that loop now lives in the runtime library instead, so
; the countdown is spelled out here.
define void @countdown(ptr %p, i8 %n) {
; CHECK-LABEL: countdown:
; The entry test, then the latch: both keep their branch immediately after the
; instruction that set the flags it reads.
; CHECK:       cmp #$00
; CHECK-NEXT:  beq
; CHECK:       cmp #$00
; CHECK-NEXT:  bne
entry:
  %z = icmp eq i8 %n, 0
  br i1 %z, label %done, label %loop

loop:
  %i = phi i8 [ %n, %entry ], [ %i.next, %loop ]
  %acc = phi i8 [ 0, %entry ], [ %acc.next, %loop ]
  %v = load volatile i8, ptr %p
  %acc.next = add i8 %acc, %v
  store volatile i8 %acc.next, ptr %p
  %i.next = add i8 %i, -1
  %c = icmp eq i8 %i.next, 0
  br i1 %c, label %done, label %loop

done:
  ret void
}
