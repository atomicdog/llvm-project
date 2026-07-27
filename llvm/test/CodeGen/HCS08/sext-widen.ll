; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Widening a word to a doubleword. Type legalization expands it into the word
; itself and an arithmetic shift right by 15, so what the backend actually has
; to be good at is the sign splat: one of two answers, 0 or -1, from one bit.
;
; It used to go through the variable-count shift loop - fifteen iterations,
; and a loop where straight-line code was expected. That second part was the
; real defect. Emitted as the argument of a call, the loop's branches landed
; between ADJCALLSTACKDOWN and the call and split the call frame across basic
; blocks, which the machine verifier rejects:
;
;   *** Bad machine code: Call frame size on entry does not match value
;       computed from predecessor ***
;
; The answers were right the whole time, so nothing miscompiled; it was an
; invariant being broken, of the kind that stops being harmless as soon as
; another pass trusts it. -verify-machineinstrs above is most of this test.

; The splat itself: the sign reaches A through the stack, lsla puts it in the
; carry, and 0 - 0 - carry is 0x00 or 0xFF, which goes to both halves.
define i16 @splat(i16 %a) {
; CHECK-LABEL: splat:
; CHECK:      pshh
; CHECK-NEXT: pula
; CHECK-NEXT: lsla
; CHECK-NEXT: clra
; CHECK-NEXT: sbc #$00
; CHECK-NEXT: tax
; CHECK-NEXT: psha
; CHECK-NEXT: pulh
; CHECK-NEXT: rts
  %r = ashr i16 %a, 15
  ret i16 %r
}

; No loop, so no branch and no frame: the whole function is the splat.
define i16 @splat_no_loop(i16 %a) {
; CHECK-LABEL: splat_no_loop:
; CHECK-NOT: ais
; CHECK-NOT: b{{eq|ne}}
; CHECK: rts
  %r = ashr i16 %a, 15
  ret i16 %r
}

define i32 @widen(i16 %a) {
; CHECK-LABEL: widen:
; CHECK: pshh
; CHECK-NOT: b{{eq|ne}}
  %r = sext i16 %a to i32
  ret i32 %r
}

; Zero extension must not pick the splat up - there is nothing to splat.
define i32 @widen_unsigned(i16 %a) {
; CHECK-LABEL: widen_unsigned:
; CHECK-NOT: sbc
  %r = zext i16 %a to i32
  ret i32 %r
}

; The shape that was actually broken: the extension is built inside the call
; sequence, so anything with control flow in it tears the frame in half.
define i32 @widen_into_call(i16 %a, i32 %b) {
; CHECK-LABEL: widen_into_call:
; CHECK: jsr __divsi3
  %s = sext i16 %a to i32
  %r = sdiv i32 %s, %b
  ret i32 %r
}

define float @widen_into_libcall_f32(i16 %a) {
; CHECK-LABEL: widen_into_libcall_f32:
; CHECK: jsr __floatsisf
  %r = sitofp i16 %a to float
  ret float %r
}

; Any other count goes to the runtime; 15 is the one that is answered here.
define i16 @variable_shift(i16 %a, i8 %n) {
; CHECK-LABEL: variable_shift:
; CHECK-NOT: pshh
; CHECK: jsr __ashrhi3
  %s = zext i8 %n to i16
  %r = ashr i16 %a, %s
  ret i16 %r
}
