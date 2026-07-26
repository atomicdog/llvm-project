; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=hcs08 -verify-machineinstrs -hcs08-dp-bank-size=8 < %s | \
; RUN:   FileCheck %s
; RUN: llc -mtriple=hcs08 -hcs08-dp-bank-size=8 -filetype=obj < %s | \
; RUN:   llvm-readobj -r - | FileCheck %s --check-prefix=RELOC

; Spill slots that are never live across a call move out of the frame and into
; a direct-page bank shared by the whole program. Every direct-page form is a
; byte shorter than the n,sp form, and unlike HCS08StackToIndexed this reaches
; sthx and ldhx, which have no useful indexed form at all.
;
; The bank is off unless asked for: the direct page is the user's to spend (see
; section 15), and the linker script has to reserve __hcs08_dp_bank before any
; of this can link.

declare i16 @sink(i16)

; The 16-bit ALU chain is the case that pays. sthx parks the operand - which is
; precisely what frees H:X - and the six accesses between it and the ldhx that
; collects the result all become direct-page.
;
; The slot is asked for at instruction selection rather than promoted out of
; the frame afterwards, so no frame object is created for it: with nothing else
; in the frame this function loses its prologue and epilogue too, which is the
; larger half of the saving. 24 bytes to 18.
define i16 @add16(i16 %a, i16 %b) {
; CHECK-LABEL: add16:
; CHECK-NOT:   ais
; CHECK:       sthx <__hcs08_dp_bank
; CHECK-NEXT:  lda <__hcs08_dp_bank+1
; CHECK-NEXT:  tsx
; CHECK-NEXT:  add $03,x
; CHECK-NEXT:  sta <__hcs08_dp_bank+1
; CHECK-NEXT:  lda <__hcs08_dp_bank
; CHECK-NEXT:  adc $02,x
; CHECK-NEXT:  sta <__hcs08_dp_bank
; CHECK-NEXT:  ldhx <__hcs08_dp_bank
; CHECK-NEXT:  rts
;
; Without the flag the slot stays in the frame, reached through H:X where that
; pass can manage it and n,sp where it cannot - and the frame it needs costs an
; ais at each end.
; OFF-LABEL: add16:
; OFF:       ais #$fe
; OFF:       sthx $01,sp
; OFF-NOT:   __hcs08_dp_bank
; OFF:       ldhx $01,sp
; OFF-NEXT:  ais #$02
  %r = add i16 %a, %b
  ret i16 %r
}

; A value that has to survive a call cannot be in the bank, because the callee
; is entitled to the same bytes. It stays in the frame; the temporaries around
; it, which die before the call and are freshly written after it, do not.
define i16 @across_call(i16 %a, i16 %b) {
; CHECK-LABEL: across_call:
; CHECK:       ldhx <__hcs08_dp_bank
; CHECK-NEXT:  sthx $03,sp
; CHECK:       jsr sink
; The first thing touching the bank after the call is a store, never a load.
; CHECK-NEXT:  sthx <__hcs08_dp_bank+4
; CHECK-NEXT:  ldhx $03,sp
; With both operands parked in the bank the chain reads them there too, which
; is what adc's direct-page form is for.
; CHECK:       lda <__hcs08_dp_bank+1
; CHECK-NEXT:  add <__hcs08_dp_bank+3
; CHECK:       adc <__hcs08_dp_bank+2
  %s = add i16 %a, %b
  %t = call i16 @sink(i16 7)
  %u = add i16 %s, %t
  ret i16 %u
}

; The bank is an undefined symbol the linker script places in the page, so the
; references are 8-bit relocations - which is what makes a bank that does not
; fit an R_HCS08_8 overflow at link time rather than a wrapped address.

; RELOC: R_HCS08_8 __hcs08_dp_bank
