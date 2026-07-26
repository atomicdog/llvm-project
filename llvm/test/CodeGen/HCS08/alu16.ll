; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Phase 1: the general 16-bit ALU. There is no 16-bit ALU on this machine
; beyond aix, so these become byte chains through A, low byte first so that add
; and sub carry into the high byte. Both operands have to be in memory, since
; H:X cannot hold two 16-bit values.
;
; The chain is emitted after register allocation, so nothing can be inserted
; between the add and the adc. lda and sta do not disturb the carry - which is
; what makes the chain work at all - but that is checked on a CPU model rather
; than assumed.
;
; H:X is dead for the whole chain - the operand is parked before it and the
; result collected after - so HCS08StackToIndexed puts the frame base there and
; every access loses its page-2 prefix. tsx does not disturb the carry either.

; The common shape: the second argument is already a frame object, so it is
; read where it lies and only the first operand needs parking.
define i16 @add16(i16 %a, i16 %b) {
; CHECK-LABEL: add16:
; CHECK:       ais #$fe
; CHECK-NEXT:  sthx $01,sp
; CHECK-NEXT:  tsx
; CHECK-NEXT:  lda $01,x
; CHECK-NEXT:  add $05,x
; CHECK-NEXT:  sta $01,x
; CHECK-NEXT:  lda $00,x
; CHECK-NEXT:  adc $04,x
; CHECK-NEXT:  sta $00,x
; CHECK-NEXT:  ldhx $01,sp
; CHECK-NEXT:  ais #$02
; CHECK-NEXT:  rts
  %r = add i16 %a, %b
  ret i16 %r
}

; Subtraction borrows through sbc in the same shape.
define i16 @sub16(i16 %a, i16 %b) {
; CHECK-LABEL: sub16:
; CHECK:       tsx
; CHECK-NEXT:  lda $01,x
; CHECK-NEXT:  sub $05,x
; CHECK-NEXT:  sta $01,x
; CHECK-NEXT:  lda $00,x
; CHECK-NEXT:  sbc $04,x
; CHECK-NEXT:  sta $00,x
  %r = sub i16 %a, %b
  ret i16 %r
}

; The logical operations have no carry to propagate, so both halves are the
; same instruction.
define i16 @and16(i16 %a, i16 %b) {
; CHECK-LABEL: and16:
; CHECK:       and $05,x
; CHECK:       and $04,x
  %r = and i16 %a, %b
  ret i16 %r
}

define i16 @or16(i16 %a, i16 %b) {
; CHECK-LABEL: or16:
; CHECK:       ora $05,x
; CHECK:       ora $04,x
  %r = or i16 %a, %b
  ret i16 %r
}

define i16 @xor16(i16 %a, i16 %b) {
; CHECK-LABEL: xor16:
; CHECK:       eor $05,x
; CHECK:       eor $04,x
  %r = xor i16 %a, %b
  ret i16 %r
}

; A variable index into a pointer is the same addition.
define ptr @index(ptr %p, i16 %i) {
; CHECK-LABEL: index:
; CHECK:       add $05,x
; CHECK:       adc $04,x
  %q = getelementptr i8, ptr %p, i16 %i
  ret ptr %q
}

; Neither operand in memory: both get parked, in the two scratch words. Both
; loads are used twice, so neither folds into the operation.
@g = global i16 0
@g2 = global i16 0
@r1 = global i16 0
@r2 = global i16 0

define void @both_in_registers() {
; CHECK-LABEL: both_in_registers:
; CHECK:       sthx $07,sp
; CHECK:       sthx $05,sp
; CHECK-NEXT:  tsx
; CHECK-NEXT:  lda $07,x
; CHECK-NEXT:  sub $05,x
; CHECK-NEXT:  sta $07,x
; CHECK-NEXT:  lda $06,x
; CHECK-NEXT:  sbc $04,x
; CHECK-NEXT:  sta $06,x
; CHECK-NEXT:  ldhx $07,sp
  %a = load i16, ptr @g
  %b = load i16, ptr @g2
  %s = sub i16 %a, %b
  %t = sub i16 %b, %a
  store i16 %s, ptr @r1
  store i16 %t, ptr @r2
  ret void
}

; A 16-bit value live across a call, then added to the result.
declare i16 @f()

define i16 @across_call(i16 %a) {
; CHECK-LABEL: across_call:
; CHECK:       jsr f
; CHECK:       add $05,x
; CHECK:       adc $04,x
  %b = call i16 @f()
  %r = add i16 %a, %b
  ret i16 %r
}

; Adding a small constant still takes the one-instruction path.
define i16 @add_constant(i16 %p) {
; CHECK-LABEL: add_constant:
; CHECK-NOT:   ais
; CHECK:       aix #$05
; CHECK-NEXT:  rts
  %r = add i16 %p, 5
  ret i16 %r
}
