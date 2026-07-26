; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Phase 1: dereferencing a pointer. H:X is the only index register, so it holds
; the pointer; a constant displacement folds into the n,x or nn,x form rather
; than being materialized with aix.

define i8 @load8(ptr %p) {
; CHECK-LABEL: load8:
; CHECK:       lda ,x
; CHECK-NEXT:  rts
  %r = load i8, ptr %p
  ret i8 %r
}

define i8 @load8_offset(ptr %p) {
; CHECK-LABEL: load8_offset:
; CHECK:       lda $05,x
; CHECK-NEXT:  rts
  %q = getelementptr i8, ptr %p, i16 5
  %r = load i8, ptr %q
  ret i8 %r
}

; Past a byte of displacement, the 16-bit indexed form takes over. Without it
; this would need a general i16 add, which aix cannot do.
define i8 @load8_far(ptr %p) {
; CHECK-LABEL: load8_far:
; CHECK-NOT:   aix
; CHECK:       lda $012c,x
; CHECK-NEXT:  rts
  %q = getelementptr i8, ptr %p, i16 300
  %r = load i8, ptr %q
  ret i8 %r
}

define void @store8(ptr %p, i8 %v) {
; CHECK-LABEL: store8:
; CHECK:       sta ,x
; CHECK-NEXT:  rts
  store i8 %v, ptr %p
  ret void
}

define void @store8_offset(ptr %p, i8 %v) {
; CHECK-LABEL: store8_offset:
; CHECK:       sta $03,x
; CHECK-NEXT:  rts
  %q = getelementptr i8, ptr %p, i16 3
  store i8 %v, ptr %q
  ret void
}

; A 16-bit load may reuse H:X for its result: the pointer is dead by then.
define i16 @load16(ptr %p) {
; CHECK-LABEL: load16:
; CHECK:       ldhx ,x
; CHECK-NEXT:  rts
  %r = load i16, ptr %p
  ret i16 %r
}

define i16 @load16_offset(ptr %p) {
; CHECK-LABEL: load16_offset:
; CHECK:       ldhx $04,x
; CHECK-NEXT:  rts
  %q = getelementptr i16, ptr %p, i16 2
  %r = load i16, ptr %q
  ret i16 %r
}

; A 16-bit store has no indexed form at all, so it goes a byte at a time
; through A, high half first - this is a big-endian target.
define void @store16(ptr %p, i16 %v) {
; CHECK-LABEL: store16:
; CHECK:       sta ,x
; CHECK:       sta $01,x
  store i16 %v, ptr %p
  ret void
}

define void @store16_offset(ptr %p, i16 %v) {
; CHECK-LABEL: store16_offset:
; CHECK:       sta $06,x
; CHECK:       sta $07,x
  %q = getelementptr i16, ptr %p, i16 3
  store i16 %v, ptr %q
  ret void
}

; The ALU reads its second operand through the pointer directly.
define i8 @alu_through_pointer(ptr %p, i8 %a) {
; CHECK-LABEL: alu_through_pointer:
; CHECK-NOT:   ais
; CHECK:       add ,x
; CHECK-NEXT:  rts
  %b = load i8, ptr %p
  %r = add i8 %a, %b
  ret i8 %r
}

define i8 @alu_through_pointer_offset(ptr %p, i8 %a) {
; CHECK-LABEL: alu_through_pointer_offset:
; CHECK:       and $07,x
; CHECK-NEXT:  rts
  %q = getelementptr i8, ptr %p, i16 7
  %b = load i8, ptr %q
  %r = and i8 %a, %b
  ret i8 %r
}

; Two accesses off one pointer, with no frame needed for either.
define i8 @two_accesses(ptr %p) {
; CHECK-LABEL: two_accesses:
; CHECK-NOT:   ais
; CHECK:       lda ,x
; CHECK-NEXT:  add $01,x
; CHECK-NEXT:  rts
  %a = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i16 1
  %b = load i8, ptr %q
  %r = add i8 %a, %b
  ret i8 %r
}

; Read-modify-write in place.
define void @increment(ptr %p) {
; CHECK-LABEL: increment:
; CHECK:       add ,x
; CHECK-NEXT:  sta ,x
  %v = load i8, ptr %p
  %n = add i8 %v, 1
  store i8 %n, ptr %p
  ret void
}

; Two 16-bit stores through one pointer, which is what a 32-bit store is. The
; pointer has to survive the first of them: the byte-at-a-time expansion used
; to mark it killed on its own last byte regardless of whether the store it
; came from was the last use, so the second half then read a register liveness
; had been told was dead. That is a machine-verifier error rather than a wrong
; instruction, which is why this test is only as strong as -verify-machineinstrs.
define void @store32(ptr %p, i32 %v) {
; CHECK-LABEL: store32:
; CHECK:       sta $02,x
; CHECK:       sta $03,x
; CHECK:       sta ,x
; CHECK:       sta $01,x
  store i32 %v, ptr %p
  ret void
}

; A global stays on the extended forms - it needs no index register.
@g = global i8 0

define i8 @global_not_indexed() {
; CHECK-LABEL: global_not_indexed:
; CHECK:       lda g
; CHECK-NEXT:  rts
  %r = load i8, ptr @g
  ret i8 %r
}
