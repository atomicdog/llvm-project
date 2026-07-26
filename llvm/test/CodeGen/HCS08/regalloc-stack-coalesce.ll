; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Greedy used to fail this with "ran out of registers" where basic succeeded,
; which looked like register pressure and was not - the pressure here is
; already relieved at instruction selection.
;
; The target did not implement isLoadFromStackSlot / isStoreToStackSlot, so
; InlineSpiller::coalesceStackAccess could never fire. When the spiller went to
; spill a value that was already in the very slot it was spilling to, it could
; not see that, and emitted a reload and a store of the same slot - a copy that
; does nothing, through H:X. That pair is an unspillable interval (weight INF),
; and with one 16-bit register two of them overlapping have nowhere to go.
;
; The test is that this compiles at all; -verify-machineinstrs is what makes it
; worth more than that.

; CHECK-LABEL: __mulosi4:
; CHECK: rts
define i32 @__mulosi4(i32 %0, i1 %1) {
  %3 = icmp eq i32 %0, 0
  br i1 %3, label %4, label %5
4:                                                ; preds = %2
  br i1 %1, label %common.ret, label %8
5:                                                ; preds = %2
  %6 = call i32 @llvm.abs.i32(i32 %0, i1 false)
  %7 = icmp slt i32 %6, 1
  br i1 %7, label %8, label %common.ret
common.ret:                                       ; preds = %8, %5, %4
  %common.ret.op = phi i32 [ %0, %8 ], [ 0, %5 ], [ 0, %4 ]
  ret i32 %common.ret.op
8:                                                ; preds = %5, %4
  br label %common.ret
}
declare i32 @llvm.abs.i32(i32, i1 immarg) #0
