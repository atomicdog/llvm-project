; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; A select expanded inside a call sequence. There is no conditional move here,
; so a select becomes a diamond, and the custom inserter that builds one splits
; the block it was in. A select is a value like any other, so it can be the
; argument of a call, and it is then expanded between the frame setup and the
; call - leaving the frame opened in one block and closed in another:
;
;   *** Bad machine code: Call frame size on entry does not match value
;       computed from predecessor ***
;
; The blocks a split creates are inside whatever their parent was inside, and
; have to say so. MachineBasicBlock carries the call frame size open on entry
; for exactly this, and every target that expands anything into control flow
; sets it - AVR, ARM, M68k all do it in their select inserters. This one now
; does too.
;
; This is the general answer. The frame is reserved on this target, so
; ADJCALLSTACKDOWN and its UP emit no code at all and the tear was never going
; to corrupt a stack; it was bookkeeping that no pass had yet relied on, which
; is the kind of thing that is harmless right up until something does.

declare i32 @sink(i32, i32)
declare i16 @sink16(i16, i16)

define i32 @select_into_call(i32 %a, i32 %b, i1 %c, i32 %d) {
; CHECK-LABEL: select_into_call:
; CHECK: jsr sink
  %v = select i1 %c, i32 %a, i32 %b
  %r = call i32 @sink(i32 %v, i32 %d)
  ret i32 %r
}

define i16 @select16_into_call(i16 %a, i16 %b, i16 %n, i16 %d) {
; CHECK-LABEL: select16_into_call:
; CHECK: jsr sink16
  %c = icmp slt i16 %n, 0
  %v = select i1 %c, i16 %a, i16 %b
  %r = call i16 @sink16(i16 %v, i16 %d)
  ret i16 %r
}

; Two of them, so the second is expanded in a block that is already a split of
; the first and has to inherit the size a second time.
define i32 @two_selects_into_call(i32 %a, i32 %b, i1 %c, i1 %e) {
; CHECK-LABEL: two_selects_into_call:
; CHECK: jsr sink
  %v = select i1 %c, i32 %a, i32 %b
  %w = select i1 %e, i32 %b, i32 %a
  %r = call i32 @sink(i32 %v, i32 %w)
  ret i32 %r
}

; The shape that turned up in __divsi3: the sign fixups around a division are
; selects, and the division is a call with stack arguments.
define i32 @signed_divide(i32 %a, i32 %b) {
; CHECK-LABEL: signed_divide:
; CHECK: jsr __divsi3
  %r = sdiv i32 %a, %b
  ret i32 %r
}
