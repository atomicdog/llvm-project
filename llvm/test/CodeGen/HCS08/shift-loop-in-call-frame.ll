; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; A shift used to compute a call argument. This is a regression test for a
; frame that came apart: the shift was expanded here as a countdown loop, by a
; custom inserter that splits the block it is in, and as the argument of a call
; that put its branches between ADJCALLSTACKDOWN and the call itself. The frame
; was then opened in one basic block and closed in another, which the machine
; verifier rejects along every edge:
;
;   *** Bad machine code: Call frame size on entry does not match value
;       computed from predecessor ***
;
; Nothing miscompiled - the answers were right - but the invariant was broken,
; and -verify-machineinstrs above is most of this test.
;
; Two separate things were wrong, and it is worth keeping them apart. The
; invariant itself is fixed in emitSelect, by telling the blocks a split
; creates how much call frame is open across them - see select-call-frame.ll,
; which is the general answer and covers every splitter. What is left here is
; the shift being expanded inline at all: it is a loop however it is written,
; so the loop was never the thing to remove, but where it was written was. One
; copy in the runtime is smaller than one at every use, by 10 to 15% across the
; whole soft-float set.

declare i32 @sink(i32, i32)

define i32 @shift_into_call(i16 %a, i8 %n, i32 %b) {
; CHECK-LABEL: shift_into_call:
; CHECK: jsr __ashrhi3
; CHECK: jsr sink
  %s = zext i8 %n to i16
  %v = ashr i16 %a, %s
  %w = sext i16 %v to i32
  %r = call i32 @sink(i32 %w, i32 %b)
  ret i32 %r
}

; The shape __divsf3 and __fixsfdi have: a variable shift feeding a libcall
; with 16 bytes of arguments.
define i64 @shift_into_libcall(i64 %a, i64 %b, i8 %n) {
; CHECK-LABEL: shift_into_libcall:
; CHECK: jsr __divdi3
  %s = zext i8 %n to i64
  %v = ashr i64 %a, %s
  %r = sdiv i64 %v, %b
  ret i64 %r
}

; A constant count is no different: it was a loop too, and its branches split
; the frame the same way.
define i32 @const_shift_into_call(i16 %a, i32 %b) {
; CHECK-LABEL: const_shift_into_call:
; CHECK: jsr sink
  %v = shl i16 %a, 3
  %w = sext i16 %v to i32
  %r = call i32 @sink(i32 %w, i32 %b)
  ret i32 %r
}

; An 8-bit variable shift is widened to 16 and uses the same routine, so it is
; covered by the same fix.
define i32 @byte_shift_into_call(i8 %a, i8 %n, i32 %b) {
; CHECK-LABEL: byte_shift_into_call:
; CHECK: jsr __ashrhi3
; CHECK: jsr sink
  %v = ashr i8 %a, %n
  %w = sext i8 %v to i32
  %r = call i32 @sink(i32 %w, i32 %b)
  ret i32 %r
}
