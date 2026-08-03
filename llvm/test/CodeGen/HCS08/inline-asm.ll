; RUN: llc -mtriple=hcs08 < %s | FileCheck %s

; Register-constrained inline assembly. clang has accepted 'a' and 'x' in
; validateAsmConstraint since the target was added, but without
; getConstraintType and getRegForInlineAsmConstraint the allocator had nothing
; to satisfy them with, and every one of these failed with "could not allocate
; output register for constraint".

; 'a' is the accumulator.
define i8 @out_a() {
; CHECK-LABEL: out_a:
; CHECK: ;APP
; CHECK-NEXT: clra
; CHECK-NEXT: ;NO_APP
  %r = call i8 asm sideeffect "clra", "=a"()
  ret i8 %r
}

define i8 @in_a(i8 %v) {
; CHECK-LABEL: in_a:
; CHECK: ;APP
; CHECK-NEXT: inca
; CHECK-NEXT: ;NO_APP
  %r = call i8 asm sideeffect "inca", "=a,a"(i8 %v)
  ret i8 %r
}

; 'x' is the H:X pair, which is the 16-bit and pointer register.
define i16 @in_x(i16 %v) {
; CHECK-LABEL: in_x:
; CHECK: ;APP
; CHECK-NEXT: aix #$01
; CHECK-NEXT: ;NO_APP
  %r = call i16 asm sideeffect "aix #$$01", "=x,x"(i16 %v)
  ret i16 %r
}

; A tied operand: the allocator has to put input and output in the same place,
; which is why these hooks return a register class rather than a fixed physreg.
define i8 @tied(i8 %v) {
; CHECK-LABEL: tied:
; CHECK: ;APP
; CHECK-NEXT: inca
; CHECK-NEXT: ;NO_APP
  %r = call i8 asm sideeffect "inca", "=a,0"(i8 %v)
  ret i8 %r
}

; Clobbers by name, including the condition codes and memory.
define void @clobbers() {
; CHECK-LABEL: clobbers:
; CHECK: ;APP
; CHECK-NEXT: nop
; CHECK-NEXT: ;NO_APP
; CHECK: rts
  call void asm sideeffect "nop", "~{a},~{ccr},~{memory}"()
  ret void
}

; The named form, which goes through the base implementation and matches
; against the register names the target already publishes.
define i16 @named_hx(i16 %v) {
; CHECK-LABEL: named_hx:
; CHECK: ;APP
; CHECK-NEXT: aix #$ff
; CHECK-NEXT: ;NO_APP
  %r = call i16 asm sideeffect "aix #$$ff", "={hx},{hx}"(i16 %v)
  ret i16 %r
}
