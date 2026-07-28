; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; The instructions that act on the CPU rather than on a value. See
; CodeGenDesign.md section 21.

declare void @llvm.hcs08.sei()
declare void @llvm.hcs08.cli()
declare i8 @llvm.hcs08.get.ccr()
declare void @llvm.hcs08.set.ccr(i8)
declare void @llvm.hcs08.wait()
declare void @llvm.hcs08.stop()
declare void @llvm.hcs08.nop()

@a = global i8 0
@b = global i8 0

; CHECK-LABEL: mask:
; CHECK: sei
; CHECK: cli
; CHECK: rts
define void @mask() {
  call void @llvm.hcs08.sei()
  call void @llvm.hcs08.cli()
  ret void
}

; CHECK-LABEL: sleep:
; CHECK: wait
define void @sleep() {
  call void @llvm.hcs08.wait()
  ret void
}

; CHECK-LABEL: deep:
; CHECK: stop
define void @deep() {
  call void @llvm.hcs08.stop()
  ret void
}

; Both must survive. nop is the one instruction here declared hasSideEffects=0
; at the MC layer, so it is selected to a code-generation-only view that says
; otherwise - without it DeadMachineInstructionElim removes the only thing the
; caller asked for.
;
; CHECK-LABEL: pad:
; CHECK: nop
; CHECK-NEXT: nop
define void @pad() {
  call void @llvm.hcs08.nop()
  call void @llvm.hcs08.nop()
  ret void
}

; tpa and tap are accumulator-implicit at the MC layer, so codegen selects
; views that carry the register.
;
; CHECK-LABEL: roundtrip:
; CHECK: tpa
; CHECK: tap
define void @roundtrip() {
  %s = call i8 @llvm.hcs08.get.ccr()
  call void @llvm.hcs08.sei()
  call void @llvm.hcs08.set.ccr(i8 %s)
  ret void
}

; The masking pair is a memory barrier, which is the entire point of a critical
; section: the store to @b stays between them, and the store to @a is not sunk
; past the sei even though nothing in the function reads it. Neither operand is
; volatile - if these were IntrNoMem the optimiser would be free to move both.
;
; CHECK-LABEL: barrier:
; CHECK: sta a
; CHECK: sei
; CHECK: sta b
; CHECK: cli
define void @barrier() {
  store i8 1, ptr @a
  call void @llvm.hcs08.sei()
  store i8 2, ptr @b
  call void @llvm.hcs08.cli()
  ret void
}
