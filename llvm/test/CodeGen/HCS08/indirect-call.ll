; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Calling a value rather than a symbol. There is a jsr ,x, and it is not usable
; here: it wants the target in H:X, which is where the first 16-bit argument
; goes, and the argument cannot be put anywhere else because the callee is what
; decides where to read it. One pointer register, two things that need it at
; the same instant.
;
; So the jump is an rts, which takes its destination off the stack and needs no
; register at all. Four bytes at the bottom of the outgoing arguments hold the
; target and, above it, the address to come back to:
;
;   sp+1, sp+2   target      popped into PC by the rts
;   sp+3, sp+4   return      left on top, where the callee's own rts finds it
;   sp+5 ...     the stack arguments, shifted up out of the way
;
; which is the picture a jsr would have left, so the callee cannot tell. The
; two stores happen before any argument register is written - that is what
; makes the collision go away - and SP does not move while they do, which is
; what keeps every other displacement in the call sequence right.

define void @no_args(ptr %f) {
; CHECK-LABEL: no_args:
; The return address goes in first, then the target below it, and only then
; does anything else happen.
; CHECK:      ldhx #.Ltmp{{[0-9]+}}
; CHECK-NEXT: sthx $03,sp
; CHECK:      sthx $01,sp
; CHECK:      rts
; CHECK-NEXT: .Ltmp{{[0-9]+}}:
; A jsr and its rts leave the stack alone because the caller pushed what the
; callee pops. Nothing was pushed here and two rts instructions ran, so the
; four bytes have to be given back.
; CHECK-NEXT: ais #$fc
  call void %f()
  ret void
}

; The collision itself: the argument wants H:X and so does the target, so the
; target must already be in memory before H:X is loaded.
define i16 @word_arg(ptr %f, i16 %a) {
; CHECK-LABEL: word_arg:
; CHECK:      sthx $01,sp
; CHECK:      ldhx ${{[0-9a-f]+}},sp
; CHECK-NEXT: rts
  %r = call i16 %f(i16 %a)
  ret i16 %r
}

; Stack arguments are shifted up by the four bytes, so the first of them is at
; sp+5 rather than sp+1.
define i16 @stack_args(ptr %f, i16 %a, i16 %b, i16 %c) {
; CHECK-LABEL: stack_args:
; CHECK-DAG: sthx $05,sp
; CHECK-DAG: sthx $07,sp
; CHECK: rts
  %r = call i16 %f(i16 %a, i16 %b, i16 %c)
  ret i16 %r
}

; A byte argument lands in A, which the sequence also has to leave alone.
define i16 @byte_and_word(ptr %f, i8 %a, i16 %b) {
; CHECK-LABEL: byte_and_word:
; CHECK: rts
  %r = call i16 %f(i8 %a, i16 %b)
  ret i16 %r
}

; A direct call is untouched by any of this and is still a jsr.
declare i16 @named(i16)
define i16 @still_direct(i16 %a) {
; CHECK-LABEL: still_direct:
; CHECK-NOT: .Ltmp
; CHECK: jsr named
  %r = call i16 @named(i16 %a)
  ret i16 %r
}

; Two calls through the same pointer need two labels, or the second return
; comes back to the first call site.
define i16 @twice(ptr %f, i16 %a) {
; CHECK-LABEL: twice:
; CHECK: ldhx #.Ltmp[[FIRST:[0-9]+]]
; CHECK: ldhx #.Ltmp[[SECOND:[0-9]+]]
; CHECK-NOT: .Ltmp[[FIRST]]:
  %x = call i16 %f(i16 %a)
  %y = call i16 %f(i16 %x)
  ret i16 %y
}

; A pointer chosen by a select - the shape that first showed the whole thing
; was missing, since it reaches ISel as a call to something that is not a
; symbol at all.
declare i16 @other(i16)
define i16 @chosen(i1 %c, i16 %a) {
; CHECK-LABEL: chosen:
; CHECK: rts
  %f = select i1 %c, ptr @named, ptr @other
  %r = call i16 %f(i16 %a)
  ret i16 %r
}
