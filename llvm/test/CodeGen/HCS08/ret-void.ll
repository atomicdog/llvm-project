; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Phase 0 code-generation bring-up: the pipeline stands up and a function that
; returns nothing lowers to a single RTS.

define void @f() {
; CHECK-LABEL: f:
; CHECK:       rts
entry:
  ret void
}
