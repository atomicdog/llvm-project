; RUN: llvm-mc -triple=hcs08 -motorola-integers -filetype=obj %s -o %t.o
; RUN: llvm-readobj -r -x .text %t.o | FileCheck %s
; RUN: llvm-mc -triple=hcs08 -motorola-integers %s | FileCheck --check-prefix=ASM %s

; The '#>expr' and '#<expr' immediate modifiers take the high and low byte of a
; (typically 16-bit) value. They are how a 16-bit address is materialized a
; byte at a time through the 8-bit accumulator, and each emits its own
; relocation against an unresolved symbol.
	lda	#>sym
	lda	#<sym

; A byte select of a value known at assembly time folds in place and needs no
; relocation: >$1234 is $12 and <$1234 is $34.
	lda	#>$1234
	lda	#<$1234

; The modifiers survive a round-trip through the assembly printer.
; ASM: #>sym
; ASM: #<sym

; CHECK:      Relocations [
; CHECK-NEXT:   Section (3) .rela.text {
; CHECK-NEXT:     0x1 R_HCS08_HI8 sym 0x0
; CHECK-NEXT:     0x3 R_HCS08_LO8 sym 0x0
; CHECK-NEXT:   }
; CHECK-NEXT: ]

; The symbol operands leave a zero placeholder byte; the constant selects are
; resolved in place.
; CHECK: 0x00000000 a600a600 a612a634
