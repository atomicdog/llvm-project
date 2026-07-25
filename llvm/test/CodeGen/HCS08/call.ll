; RUN: llc -mtriple=hcs08 < %s | FileCheck %s

; Phase 1: calls and the register-based calling convention. The first i8
; argument is passed in A and the i8 result returned in A, so an identity
; forward reduces to a bare call.

declare void @ext()
declare i8 @extret()
declare void @exttake(i8)
declare i8 @extid(i8)

define void @callvoid() {
; CHECK-LABEL: callvoid:
; CHECK:       jsr ext
; CHECK-NEXT:  rts
  call void @ext()
  ret void
}

define i8 @callret() {
; CHECK-LABEL: callret:
; CHECK:       jsr extret
; CHECK-NEXT:  rts
  %x = call i8 @extret()
  ret i8 %x
}

define void @calltake() {
; CHECK-LABEL: calltake:
; CHECK:       lda #$2a
; CHECK-NEXT:  jsr exttake
; CHECK-NEXT:  rts
  call void @exttake(i8 42)
  ret void
}

define i8 @callid(i8 %x) {
; CHECK-LABEL: callid:
; CHECK:       jsr extid
; CHECK-NEXT:  rts
  %r = call i8 @extid(i8 %x)
  ret i8 %r
}
