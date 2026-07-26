; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=hcs08 -verify-machineinstrs -hcs08-no-stack-to-indexed \
; RUN:   < %s | FileCheck %s --check-prefix=SP

; Frame access through the index register. Every n,sp form is on page 2 and its
; page-2 opcode byte is the n,x one, so the same access through H:X is a byte
; shorter: "lda $03,sp" is 9E E6 03 against "lda $02,x" at E6 02. tsx yields
; SP+1, which is where the displacement loses one.
;
; tsx costs a byte of its own, so the rewrite waits for a run of two, and it
; needs H:X - the only index register there is - to be dead across the run.

; One access is a wash: two bytes plus the tsx is what the n,sp form already
; costs, so it is left alone.
define i8 @one_access(i8 %a, i8 %b, i8 %c, i8 %d) {
; CHECK-LABEL: one_access:
; CHECK-NOT:   tsx
; CHECK:       lda $05,sp
; CHECK-NEXT:  rts
  ret i8 %d
}

; Two is where it starts paying.
define i8 @two_accesses(i8 %a, i8 %b, i8 %c) {
; CHECK-LABEL: two_accesses:
; CHECK:       tsx
; CHECK-NEXT:  lda $02,x
; CHECK-NEXT:  add $03,x
; CHECK-NEXT:  rts
;
; SP-LABEL: two_accesses:
; SP-NOT:    tsx
; SP:        lda $03,sp
; SP-NEXT:   add $04,sp
  %t = add i8 %b, %c
  ret i8 %t
}

; H:X is holding the pointer, so there is nowhere to put the frame base and the
; access stays where it was.
define void @hx_live(ptr %p, i8 %a, i8 %b) {
; CHECK-LABEL: hx_live:
; CHECK-NOT:   tsx
; CHECK:       add $03,sp
; CHECK-NEXT:  sta ,x
  %t = add i8 %a, %b
  store i8 %t, ptr %p
  ret void
}

; A call clobbers H:X, so a run stops there rather than carrying a frame base
; across it.
declare void @f()

define i8 @across_call(i8 %a, i8 %b, i8 %c) {
; CHECK-LABEL: across_call:
; CHECK-NOT:   tsx
; CHECK:       jsr f
; CHECK-NEXT:  tsx
  %t = add i8 %b, %c
  call void @f()
  %u = add i8 %t, %b
  ret i8 %u
}

; The 16-bit forms gain nothing and are left alone: ldhx n,x is itself on page
; 2, the same three bytes as ldhx n,sp, and sthx has no indexed form at all.
; The byte chain between them is the best run there is - H:X is dead for all of
; it, because that is the whole reason the operand was parked.
define i16 @word_frame(i16 %a, i16 %b) {
; CHECK-LABEL: word_frame:
; CHECK:       sthx $01,sp
; CHECK-NEXT:  tsx
; CHECK-NEXT:  lda $01,x
; CHECK-NEXT:  add $05,x
; CHECK-NEXT:  sta $01,x
; CHECK-NEXT:  lda $00,x
; CHECK-NEXT:  adc $04,x
; CHECK-NEXT:  sta $00,x
; CHECK-NEXT:  ldhx $01,sp
  %r = add i16 %a, %b
  ret i16 %r
}
