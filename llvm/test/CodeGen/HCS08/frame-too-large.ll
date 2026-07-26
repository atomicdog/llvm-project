; RUN: not llc -mtriple=hcs08 < %s -o /dev/null 2>&1 | FileCheck %s

; Over 255 there is no n,sp form that reaches the object. lda and sta have
; $nnnn,sp and ldhx, sthx and cphx do not, so the general answer is the address
; computed into H:X, and neither is implemented. Refusing is the one thing that
; must not be a silent wrong access - which is what this was in a release
; build, where the assert that used to stand here was compiled out and the low
; byte of the offset got written instead. See frame-large.ll for the sizes that
; do work.

define void @toobig(i8 %v) {
; CHECK: HCS08 frame object out of reach at SP+301 in 'toobig'
  %one = alloca i8
  %big = alloca [300 x i8]
  store i8 %v, ptr %one
  %p = getelementptr [300 x i8], ptr %big, i16 0, i16 0
  store i8 %v, ptr %p
  ret void
}
