; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=hcs08 -filetype=obj < %s | llvm-readobj -r - | \
; RUN:   FileCheck %s --check-prefix=RELOC

; Direct-page addressing: the same operations as the extended forms, one byte
; shorter, for the two kinds of address that are known below $0100 at compile
; time - a variable the user has placed in the .page0 section, and an absolute
; address, which is how a memory-mapped register is written.
;
; A symbol is printed with the '<' that forces the direct-page form, because
; the assembler cannot evaluate it and would otherwise pick the extended one.

@dp8 = global i8 0, section ".page0"
@dp16 = global i16 0, section ".page0"
@dpsub = global i8 0, section ".page0.dpsub"
@far8 = global i8 0

define i8 @load_dp8() {
; CHECK-LABEL: load_dp8:
; CHECK:       lda <dp8
; CHECK-NEXT:  rts
  %v = load i8, ptr @dp8
  ret i8 %v
}

define void @store_dp8() {
; CHECK-LABEL: store_dp8:
; CHECK:       lda #$07
; CHECK-NEXT:  sta <dp8
; CHECK-NEXT:  rts
  store i8 7, ptr @dp8
  ret void
}

define i16 @load_dp16() {
; CHECK-LABEL: load_dp16:
; CHECK:       ldhx <dp16
; CHECK-NEXT:  rts
  %v = load i16, ptr @dp16
  ret i16 %v
}

define void @store_dp16(i16 %v) {
; CHECK-LABEL: store_dp16:
; CHECK:       sthx <dp16
; CHECK-NEXT:  rts
  store i16 %v, ptr @dp16
  ret void
}

; A subsection of .page0 is in the page too, so that -fdata-sections-style
; names and a hand-split page both work.
define i8 @load_dpsub() {
; CHECK-LABEL: load_dpsub:
; CHECK:       lda <dpsub
; CHECK-NEXT:  rts
  %v = load i8, ptr @dpsub
  ret i8 %v
}

; Everything else stays extended: the compiler has no idea where it will land.
define i8 @load_far8() {
; CHECK-LABEL: load_far8:
; CHECK:       lda far8
; CHECK-NEXT:  rts
  %v = load i8, ptr @far8
  ret i8 %v
}

define i8 @add_dp8(i8 %a) {
; CHECK-LABEL: add_dp8:
; CHECK:       add <dp8
; CHECK-NEXT:  rts
  %v = load i8, ptr @dp8
  %r = add i8 %a, %v
  ret i8 %r
}

define i8 @cmp_dp8(i8 %a) {
; CHECK-LABEL: cmp_dp8:
; CHECK:       cmp <dp8
; CHECK-NEXT:  bcs
  %v = load i8, ptr @dp8
  %c = icmp ult i8 %a, %v
  %r = select i1 %c, i8 1, i8 2
  ret i8 %r
}

define i8 @cmp_dp16(i16 %a) {
; CHECK-LABEL: cmp_dp16:
; CHECK:       cphx <dp16
; CHECK-NEXT:  bcs
  %v = load i16, ptr @dp16
  %c = icmp ult i16 %a, %v
  %r = select i1 %c, i8 1, i8 2
  ret i8 %r
}

; A fixed address needs no relocation, so the byte is in the instruction and
; there is nothing to force: an operand that fits in one selects the mode.
define void @mmio_write() {
; CHECK-LABEL: mmio_write:
; CHECK:       lda #$ff
; CHECK-NEXT:  sta $01
; CHECK-NEXT:  rts
  store volatile i8 -1, ptr inttoptr (i16 1 to ptr)
  ret void
}

define i8 @mmio_read() {
; CHECK-LABEL: mmio_read:
; CHECK:       lda $ff
; CHECK-NEXT:  rts
  %v = load volatile i8, ptr inttoptr (i16 255 to ptr)
  ret i8 %v
}

; Above the page it is the extended form - which is still a byte shorter than
; materializing the address into H:X and dereferencing it there, and leaves the
; index register alone.
define void @mmio_far() {
; CHECK-LABEL: mmio_far:
; CHECK:       lda #$ff
; CHECK-NEXT:  sta $1234
; CHECK-NEXT:  rts
  store volatile i8 -1, ptr inttoptr (i16 4660 to ptr)
  ret void
}

; The direct-page symbol references are 8-bit relocations, which is what makes
; the linker check that the variable really did land in the page: a .page0
; section placed above $00FF is an R_HCS08_8 overflow rather than a silently
; truncated address.

; RELOC:      R_HCS08_8 dp8
; RELOC-NEXT: R_HCS08_8 dp8
; RELOC-NEXT: R_HCS08_8 dp16
; RELOC-NEXT: R_HCS08_8 dp16
; RELOC-NEXT: R_HCS08_8 dpsub
; RELOC-NEXT: R_HCS08_16 far8
; RELOC-NEXT: R_HCS08_8 dp8
; RELOC-NEXT: R_HCS08_8 dp8
; RELOC-NEXT: R_HCS08_8 dp16
