; RUN: llc -mtriple=hcs08 < %s | FileCheck %s

; Phase 1: stack frames and register spilling. With A the only allocatable
; 8-bit register, a value live across a reuse of A is spilled to a stack slot
; and reloaded. The prologue/epilogue allocate and free the frame with ais, and
; stack slots are addressed SP-relative.
;
; n,sp addresses SP+n and SP sits one byte below the last thing pushed, so a
; one-byte frame puts its only slot at 1,sp. 0,sp would be the byte the next
; push takes - a slot there would be destroyed by the next call.

@g = global i8 0
@g2 = global i8 0
@g3 = global i8 0

define void @spill() {
; CHECK-LABEL: spill:
; CHECK:       ais #$ff
; CHECK:       lda g
; CHECK-NEXT:  sta $01,sp
; CHECK:       add #$01
; CHECK:       lda $01,sp
; CHECK-NEXT:  add #$02
; CHECK:       ais #$01
; CHECK-NEXT:  rts
  %a = load i8, ptr @g
  %b = add i8 %a, 1
  store i8 %b, ptr @g2
  %c = add i8 %a, 2
  store i8 %c, ptr @g3
  ret void
}
