; RUN: llc -mtriple=hcs08 -O2 < %s | FileCheck %s
; RUN: llc -mtriple=hcs08 -O0 < %s | FileCheck %s --check-prefix=NOFOLD

; Setting and clearing one bit of a direct-page location.
;
; This is what a device driver spends its time doing, and before these patterns
; every one of them was lda/ora/sta - six bytes where the instruction is two,
; and a read-modify-write that also clobbered the condition codes.

@dp = external global i8, section ".page0"

define void @set_bit2() {
; CHECK-LABEL: set_bit2:
; CHECK: bset 2,${{[0-9a-f]+}}
; CHECK-NEXT: rts
  %p = inttoptr i16 8 to ptr
  %v = load volatile i8, ptr %p
  %o = or i8 %v, 4
  store volatile i8 %o, ptr %p
  ret void
}

define void @clear_bit2() {
; CHECK-LABEL: clear_bit2:
; CHECK: bclr 2,${{[0-9a-f]+}}
; CHECK-NEXT: rts
  %p = inttoptr i16 8 to ptr
  %v = load volatile i8, ptr %p
  %a = and i8 %v, -5                       ; 0xFB
  store volatile i8 %a, ptr %p
  ret void
}

; Bit 7, to pin that the opcode arithmetic is right at the top of the range and
; that the mask comparison is unsigned - 0x80 sign-extends to -128 in the DAG.
define void @set_bit7() {
; CHECK-LABEL: set_bit7:
; CHECK: bset 7,${{[0-9a-f]+}}
  %p = inttoptr i16 9 to ptr
  %v = load volatile i8, ptr %p
  %o = or i8 %v, -128                      ; 0x80
  store volatile i8 %o, ptr %p
  ret void
}

define void @clear_bit7() {
; CHECK-LABEL: clear_bit7:
; CHECK: bclr 7,${{[0-9a-f]+}}
  %p = inttoptr i16 9 to ptr
  %v = load volatile i8, ptr %p
  %a = and i8 %v, 127                      ; 0x7F
  store volatile i8 %a, ptr %p
  ret void
}

; A variable the user placed in the direct page, reached by name. The `<` is
; the traditional Freescale spelling that forces the direct-page form, and the
; compiler emits it rather than leaving the assembler to guess from a symbol
; whose value it does not know yet.
define void @set_page0_global() {
; CHECK-LABEL: set_page0_global:
; CHECK: bset 0,<dp
  %v = load volatile i8, ptr @dp
  %o = or i8 %v, 1
  store volatile i8 %o, ptr @dp
  ret void
}

; --- negatives -------------------------------------------------------------

; Two bits at once is not one bset. Emitting two would need the manual
; selector; a Pat cannot expand to a pair.
define void @two_bits_stays_ora() {
; CHECK-LABEL: two_bits_stays_ora:
; CHECK-NOT: bset
; CHECK: ora
  %p = inttoptr i16 8 to ptr
  %v = load volatile i8, ptr %p
  %o = or i8 %v, 5
  store volatile i8 %o, ptr %p
  ret void
}

; Above the direct page there is no bset form at all.
define void @outside_page0_stays_ora() {
; CHECK-LABEL: outside_page0_stays_ora:
; CHECK-NOT: bset
; CHECK: ora
  %p = inttoptr i16 256 to ptr
  %v = load volatile i8, ptr %p
  %o = or i8 %v, 4
  store volatile i8 %o, ptr %p
  ret void
}

; Loading from one address and storing to another is not a read-modify-write,
; and the same-node check on the address is what catches it.
define void @load_a_store_b_not_fused() {
; CHECK-LABEL: load_a_store_b_not_fused:
; CHECK-NOT: bset
  %pa = inttoptr i16 8 to ptr
  %pb = inttoptr i16 9 to ptr
  %v = load volatile i8, ptr %pa
  %o = or i8 %v, 4
  store volatile i8 %o, ptr %pb
  ret void
}

; At -O0 nothing folds: IsLegalToFold refuses anything with a chain at
; CodeGenOptLevel::None. Recorded here so the behaviour is documented rather
; than discovered, and because the SDK's GPIO layer relies on knowing it - it
; is why hcs08_gpio_set is not advertised as interrupt-atomic.
; NOFOLD-LABEL: set_bit2:
; NOFOLD-NOT: bset
