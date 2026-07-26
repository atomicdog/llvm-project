; RUN: llvm-mc -triple=hcs08 -motorola-integers -show-encoding %s | FileCheck %s
; RUN: llvm-mc -triple=hcs08 -motorola-integers -filetype=obj %s -o %t.o
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC

; Which of the two absolute addressing modes an operand selects normally
; follows from its value, which leaves a symbol - whose value the assembler
; does not know - always extended. '<' and '>' say so explicitly instead, the
; traditional Freescale spelling. '<' is the one that matters: it is how a
; reference to a variable in the direct page survives being written out as
; assembly and read back in, and so what the compiler emits for one.

; CHECK: lda <dpsym                          ; encoding: [0xb6,A]
; CHECK: sta <dpsym                          ; encoding: [0xb7,A]
; CHECK: add <dpsym                          ; encoding: [0xbb,A]
; CHECK: ldhx <dpsym                         ; encoding: [0x55,A]
; CHECK: sthx <dpsym                         ; encoding: [0x35,A]
; CHECK: cphx <dpsym                         ; encoding: [0x75,A]
	lda	<dpsym
	sta	<dpsym
	add	<dpsym
	ldhx	<dpsym
	sthx	<dpsym
	cphx	<dpsym

; '>' forces the extended form, which is what an unadorned symbol gets anyway,
; but not what a small constant would.
; CHECK: lda $0040                           ; encoding: [0xc6,0x00,0x40]
; CHECK: lda far                             ; encoding: [0xc6,A,A]
	lda	>$40
	lda	>far

; Without a prefix the value decides: a byte is direct, anything wider or
; unknown is extended.
; CHECK: lda $40                             ; encoding: [0xb6,0x40]
; CHECK: lda $1234                           ; encoding: [0xc6,0x12,0x34]
; CHECK: lda far                             ; encoding: [0xc6,A,A]
	lda	$40
	lda	$1234
	lda	far

; A '<' on an address the assembler can evaluate still has to fit.
; CHECK: lda $40                             ; encoding: [0xb6,0x40]
	lda	<$40

; The forced form decides the relocation, and an 8-bit one is what makes the
; linker check that the symbol really is in the page.
; RELOC:      0x1 R_HCS08_8 dpsym 0x0
; RELOC-NEXT: 0x3 R_HCS08_8 dpsym 0x0
; RELOC-NEXT: 0x5 R_HCS08_8 dpsym 0x0
; RELOC-NEXT: 0x7 R_HCS08_8 dpsym 0x0
; RELOC-NEXT: 0x9 R_HCS08_8 dpsym 0x0
; RELOC-NEXT: 0xB R_HCS08_8 dpsym 0x0
; RELOC-NEXT: 0x10 R_HCS08_16 far 0x0
; RELOC-NEXT: 0x18 R_HCS08_16 far 0x0
