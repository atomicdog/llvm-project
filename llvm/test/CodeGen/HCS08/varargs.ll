; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Variadic functions. A variadic one passes everything on the stack, named
; arguments included: va_arg walks the unnamed ones, so they have to be
; contiguous and in the order they were written, and a register cannot be part
; of that. Keeping the named ones in registers and only spilling the unnamed
; would also have the two ends disagree about A whenever the first unnamed
; argument is byte-sized and no named one claimed it. Both ends read "..." off
; the prototype, which C requires, so both pick the same rule.
;
; The va_list is a plain pointer and nothing else, so va_arg, va_copy and
; va_end are all the generic expansions - a load, a load, and a store of the
; pointer moved past what it read. Nothing on this target is aligned to more
; than a byte, so none of that has padding to step over, and va_start is the
; only piece the target has to lower.

@sink = external global ptr

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)
declare void @llvm.va_copy.p0(ptr, ptr)

; va_start is the address of the first byte past the named arguments. One
; named word, so the list starts two bytes into the incoming area - and tsx
; puts SP+1 in H:X, which the displacement is measured from.
define void @start_after_one_word(i16 %n, ...) {
; CHECK-LABEL: start_after_one_word:
; CHECK: tsx
; CHECK: aix
; CHECK: sthx
  %ap = alloca ptr
  call void @llvm.va_start.p0(ptr %ap)
  %p = load ptr, ptr %ap
  store ptr %p, ptr @sink
  call void @llvm.va_end.p0(ptr %ap)
  ret void
}

; With three named words in front of it the list starts six bytes further on,
; so the two functions cannot share a displacement.
define void @start_after_three_words(i16 %a, i16 %b, i16 %c, ...) {
; CHECK-LABEL: start_after_three_words:
; CHECK: aix
  %ap = alloca ptr
  call void @llvm.va_start.p0(ptr %ap)
  %p = load ptr, ptr %ap
  store ptr %p, ptr @sink
  call void @llvm.va_end.p0(ptr %ap)
  ret void
}

; A named argument of a variadic function is on the stack, not in H:X, which is
; the whole point of the separate convention. Nothing loads it from a register.
define i16 @named_arg_is_on_the_stack(i16 %n, ...) {
; CHECK-LABEL: named_arg_is_on_the_stack:
; CHECK: ldhx ${{[0-9a-f]+}},sp
  ret i16 %n
}

; And the caller puts it there rather than in H:X.
declare i16 @variadic(i16, ...)
define i16 @call_variadic(i16 %a, i16 %b) {
; CHECK-LABEL: call_variadic:
; CHECK-DAG: sthx $01,sp
; CHECK-DAG: sthx $03,sp
; CHECK: jsr variadic
  %r = call i16 (i16, ...) @variadic(i16 %a, i16 %b)
  ret i16 %r
}

; The contrast, on one argument so there is nothing else to look at: a
; variadic call puts its single word on the stack, a plain one leaves it in
; H:X and stores nothing at all.
declare i16 @variadic1(i16, ...)
define i16 @call_variadic_one(i16 %a) {
; CHECK-LABEL: call_variadic_one:
; CHECK: sthx $01,sp
; CHECK: jsr variadic1
  %r = call i16 (i16, ...) @variadic1(i16 %a)
  ret i16 %r
}

declare i16 @plain1(i16)
define i16 @call_plain_one(i16 %a) {
; CHECK-LABEL: call_plain_one:
; CHECK-NOT: sthx
; CHECK: jsr plain1
  %r = call i16 @plain1(i16 %a)
  ret i16 %r
}

; va_copy is a pointer copy, so no call to memcpy comes out of it.
define void @copy(ptr %dst, ptr %src) {
; CHECK-LABEL: copy:
; CHECK-NOT: jsr memcpy
  call void @llvm.va_copy.p0(ptr %dst, ptr %src)
  ret void
}

; Both call features at once: a variadic function reached through a pointer.
; The indirect sequence reserves four bytes at the bottom of the outgoing
; arguments and the variadic convention stacks everything above them, so the
; first argument lands at sp+5 rather than sp+1.
define i16 @indirect_variadic(ptr %f, i16 %a, i16 %b) {
; CHECK-LABEL: indirect_variadic:
; CHECK-DAG: sthx $05,sp
; CHECK-DAG: sthx $07,sp
; CHECK: rts
  %r = call i16 (i16, ...) %f(i16 %a, i16 %b)
  ret i16 %r
}
