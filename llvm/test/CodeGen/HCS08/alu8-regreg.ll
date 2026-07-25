; RUN: llc -mtriple=hcs08 < %s | FileCheck %s

; Phase 1: register/register ALU. HCS08 has no reg-reg arithmetic, so the
; second operand is spilled and its reload folded into the stack (n,sp) form of
; the operation. Both %a and %b are used twice, so neither can fold as a
; single-use global load.

@g = global i8 0
@g2 = global i8 0
@r1 = global i8 0
@r2 = global i8 0

define void @subs() {
; CHECK-LABEL: subs:
; CHECK:       sub ${{[0-9a-f]+}},sp
; CHECK:       sub ${{[0-9a-f]+}},sp
  %a = load i8, ptr @g
  %b = load i8, ptr @g2
  %s = sub i8 %a, %b
  %t = sub i8 %b, %a
  store i8 %s, ptr @r1
  store i8 %t, ptr @r2
  ret void
}
