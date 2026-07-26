; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; Frames past a byte. Three separate things truncated silently here, and a
; release build has no assertions to catch any of them:
;
;   - ais takes a *signed* byte, so a frame over 127 wrapped. 128 was the worst
;     of it, because -128 encodes but +128 does not: the epilogue subtracted
;     where it meant to add and left SP 256 bytes low.
;   - the frame-index displacement is unsigned to 255, but instruction
;     selection built it as an i8 node and it reached the MachineOperand
;     through getSExtValue, so 198 arrived as -58 and addressed a different
;     object.
;   - past 255 nothing can address the object at all, and the low byte of the
;     offset was written instead. That case is frame-too-large.ll.

; Exactly 128: one ais down, two back up.
define void @frame128(i8 %v) {
; CHECK-LABEL: frame128:
; CHECK:       ais #$80
; CHECK:       sta $01,sp
; CHECK-NEXT:  ais #$7f
; CHECK-NEXT:  ais #$01
; CHECK-NEXT:  rts
  %buf = alloca [128 x i8]
  %p = getelementptr [128 x i8], ptr %buf, i16 0, i16 0
  store i8 %v, ptr %p
  ret void
}

; 200 bytes, reached at displacement 198 - which is the half of the byte that
; used to come back negative.
define void @disp198(i8 %v) {
; CHECK-LABEL: disp198:
; CHECK:       ais #$80
; CHECK-NEXT:  ais #$b8
; CHECK-NEXT:  sta $c6,sp
; CHECK-NEXT:  ais #$7f
; CHECK-NEXT:  ais #$49
; CHECK-NEXT:  rts
  %buf = alloca [200 x i8]
  %p = getelementptr [200 x i8], ptr %buf, i16 0, i16 197
  store i8 %v, ptr %p
  ret void
}
