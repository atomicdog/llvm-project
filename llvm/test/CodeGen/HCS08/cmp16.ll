; RUN: llc -mtriple=hcs08 < %s | FileCheck %s

; Phase 1: 16-bit comparisons. cphx compares the whole of H:X at once, so
; unlike a 16-bit addition this needs no byte chain - only an operand in
; memory, since there is no reg-reg form and nowhere to put a second word.
;
; Selecting on the result needs a branch either way: there is no conditional
; move, and with one register per class the two candidates could not both be
; held anyway.

define void @branch_imm(i16 %a, ptr %p) {
; CHECK-LABEL: branch_imm:
; CHECK:       cphx #$03e8
; CHECK-NEXT:  blt
  %c = icmp sge i16 %a, 1000
  br i1 %c, label %t, label %f
t:
  store i8 1, ptr %p
  ret void
f:
  store i8 2, ptr %p
  ret void
}

; An operand that is a frame object is compared where it lies.
define void @branch_stack(i16 %a, i16 %b, ptr %p) {
; CHECK-LABEL: branch_stack:
; CHECK:       cphx ${{[0-9a-f]+}},sp
; CHECK-NEXT:  b{{[a-z]+}}
  %c = icmp ult i16 %a, %b
  br i1 %c, label %t, label %f
t:
  store i8 1, ptr %p
  ret void
f:
  store i8 2, ptr %p
  ret void
}

@g = global i16 0

define void @branch_global(i16 %a, ptr %p) {
; CHECK-LABEL: branch_global:
; CHECK:       cphx g
  %b = load i16, ptr @g
  %c = icmp eq i16 %a, %b
  br i1 %c, label %t, label %f
t:
  store i8 1, ptr %p
  ret void
f:
  store i8 2, ptr %p
  ret void
}

; The condition picks a signed or an unsigned branch off the same compare.
; (>= N is canonicalized to > N-1 before it gets here.)
define i8 @signed_vs_unsigned(i16 %a) {
; CHECK-LABEL: signed_vs_unsigned:
; CHECK:       cphx #$0063
; CHECK-NEXT:  bgt
  %c = icmp sge i16 %a, 100
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @unsigned_compare(i16 %a) {
; CHECK-LABEL: unsigned_compare:
; CHECK:       cphx #$0063
; CHECK-NEXT:  bhi
  %c = icmp uge i16 %a, 100
  %r = zext i1 %c to i8
  ret i8 %r
}

; A comparison result in a register is a select between 1 and 0.
define i8 @setcc(i16 %a, i16 %b) {
; CHECK-LABEL: setcc:
; CHECK:       cphx ${{[0-9a-f]+}},sp
; CHECK-NEXT:  blt
; CHECK:       lda #$00
; CHECK:       lda #$01
  %c = icmp slt i16 %a, %b
  %r = zext i1 %c to i8
  ret i8 %r
}

; Selecting between two 16-bit values.
define i16 @clamp_low(i16 %a) {
; CHECK-LABEL: clamp_low:
; CHECK:       cphx #$0000
; CHECK-NEXT:  bge
; CHECK:       ldhx #$0000
  %c = icmp slt i16 %a, 0
  %r = select i1 %c, i16 0, i16 %a
  ret i16 %r
}

; 8-bit comparison against a register operand, which has no form of its own
; either: the custom inserter parks it, exactly as the reg-reg ALU is parked.
define i8 @umax8(i8 %a, i8 %b) {
; CHECK-LABEL: umax8:
; CHECK:       cmp ${{[0-9a-f]+}},sp
; CHECK-NEXT:  bhi
  %c = icmp ugt i8 %a, %b
  %r = select i1 %c, i8 %a, i8 %b
  ret i8 %r
}

; Choosing between two values that live in the frame: DAG combining turns the
; select of two loads into a load of a selected address, so the addresses have
; to be materializable. tsx reaches SP+1 and aix walks from there.
define i16 @select_two_locals(i16 %a, i16 %b, i16 %x, i16 %y) {
; CHECK-LABEL: select_two_locals:
; CHECK:       tsx
; CHECK-NEXT:  aix #${{[0-9a-f]+}}
; CHECK:       ldhx ,x
  %c = icmp eq i16 %a, %b
  %r = select i1 %c, i16 %x, i16 %y
  ret i16 %r
}

; The address of a local, which needs the same materialization.
declare i8 @deref(ptr)

define i8 @address_of_local() {
; CHECK-LABEL: address_of_local:
; CHECK:       tsx
; CHECK-NEXT:  jsr deref
  %p = alloca i8
  store i8 99, ptr %p
  %r = call i8 @deref(ptr %p)
  ret i8 %r
}
