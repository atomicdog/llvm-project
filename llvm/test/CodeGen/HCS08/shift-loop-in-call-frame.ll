; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s
; XFAIL: *

; A variable-count shift used to compute a call argument tears the call frame
; across basic blocks:
;
;   *** Bad machine code: Call frame size on entry does not match value
;       computed from predecessor ***
;   Call frame size on entry 0 does not match value computed from predecessor 16
;
; The count is not known, so the shift is a countdown loop, built by a custom
; inserter that splits the block it is in. Scheduled between ADJCALLSTACKDOWN
; and the call - which is where it belongs, being an operand of that call - it
; leaves the frame opened in one block and closed in another, and the machine
; verifier requires the two to match along every edge.
;
; This is the same defect that sext-widen.ll describes and the same one it
; fixes, but only for its own case: widening a word is "ashr 15", a constant,
; so it could be answered with a straight-line sign splat and the loop simply
; stopped happening. A genuinely variable count still needs the loop, so that
; answer does not generalise. Fixing it means either expanding the loop after
; PrologEpilogInserter has already removed the ADJCALLSTACK pseudos, or
; keeping the shift out of the call sequence to begin with.
;
; As with the constant case, the code that comes out is correct: __divsf3 and
; __fixsfdi both contain this shape, and every division and every f32-to-i64
; conversion in the simulator matrix passes. It is a broken invariant, not a
; wrong answer.

declare i32 @sink(i32, i32)

define i32 @shift_into_call(i16 %a, i8 %n, i32 %b) {
; CHECK-LABEL: shift_into_call:
; CHECK: jsr sink
  %s = zext i8 %n to i16
  %v = ashr i16 %a, %s
  %w = sext i16 %v to i32
  %r = call i32 @sink(i32 %w, i32 %b)
  ret i32 %r
}

; The shape __divsf3 and __fixsfdi actually have: a variable shift feeding a
; libcall with 16 bytes of arguments.
define i64 @shift_into_libcall(i64 %a, i64 %b, i8 %n) {
; CHECK-LABEL: shift_into_libcall:
; CHECK: jsr __divdi3
  %s = zext i8 %n to i64
  %v = ashr i64 %a, %s
  %r = sdiv i64 %v, %b
  ret i64 %r
}
