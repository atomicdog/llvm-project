; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Phase 1: the hardware multiply and divide, and shifting by a variable amount.
;
; mul and div are single instructions, but both work on X as well as A, so the
; second operand has to reach X - which ldx does straight from memory, and
; memory is where the second operand of anything on this machine ends up. div
; divides H:A by X, so a byte divide needs H cleared first and gets its
; remainder back in H, which only the stack can reach.

define i8 @multiply(i8 %a, i8 %b) {
; CHECK-LABEL: multiply:
; CHECK-NOT:   clrh
; CHECK:       ldx ${{[0-9a-f]+}},sp
; CHECK:       mul
; CHECK-NEXT:  ais
  %r = mul i8 %a, %b
  ret i8 %r
}

define i8 @divide(i8 %a, i8 %b) {
; CHECK-LABEL: divide:
; CHECK:       clrh
; CHECK-NEXT:  ldx ${{[0-9a-f]+}},sp
; CHECK:       div
; CHECK-NEXT:  ais
  %r = udiv i8 %a, %b
  ret i8 %r
}

; The remainder is in H, so it comes back through the stack.
define i8 @remainder(i8 %a, i8 %b) {
; CHECK-LABEL: remainder:
; CHECK:       div
; CHECK-NEXT:  pshh
; CHECK-NEXT:  pula
  %r = urem i8 %a, %b
  ret i8 %r
}

; A variable count is a countdown loop over the single-bit shift. The count
; lives in a frame byte so that A stays free for the value, and a count of zero
; has to skip the loop rather than wrap around.
define i8 @shift_left(i8 %a, i8 %b) {
; CHECK-LABEL: shift_left:
; CHECK:       tst ${{[0-9a-f]+}},x
; CHECK-NEXT:  beq
; CHECK:       lsla
; CHECK-NEXT:  dec ${{[0-9a-f]+}},sp
; CHECK-NEXT:  bne
  %r = shl i8 %a, %b
  ret i8 %r
}

define i8 @shift_right_logical(i8 %a, i8 %b) {
; CHECK-LABEL: shift_right_logical:
; CHECK:       lsra
; CHECK-NEXT:  dec ${{[0-9a-f]+}},sp
  %r = lshr i8 %a, %b
  ret i8 %r
}

define i8 @shift_right_arithmetic(i8 %a, i8 %b) {
; CHECK-LABEL: shift_right_arithmetic:
; CHECK:       asra
; CHECK-NEXT:  dec ${{[0-9a-f]+}},sp
  %r = ashr i8 %a, %b
  ret i8 %r
}

; A constant count still unrolls rather than looping.
define i8 @shift_constant(i8 %a) {
; CHECK-LABEL: shift_constant:
; CHECK-NOT:   dec
; CHECK:       lsla
; CHECK-NEXT:  lsla
; CHECK-NEXT:  lsla
; CHECK-NEXT:  rts
  %r = shl i8 %a, 3
  ret i8 %r
}

; Anything wider than the hardware provides, and signed division, is a call
; into a runtime library that does not exist yet - a link error rather than a
; compiler crash.
define i16 @multiply16(i16 %a, i16 %b) {
; CHECK-LABEL: multiply16:
; CHECK:       jsr __mulhi3
  %r = mul i16 %a, %b
  ret i16 %r
}

define i8 @divide_signed(i8 %a, i8 %b) {
; CHECK-LABEL: divide_signed:
; CHECK:       jsr __divqi3
  %r = sdiv i8 %a, %b
  ret i8 %r
}
