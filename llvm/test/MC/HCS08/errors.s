; RUN: not llvm-mc -triple=hcs08 -motorola-integers %s 2>&1 | FileCheck %s

; CHECK: error: invalid instruction mnemonic
	frobnicate

; There is no immediate form of a store.
; CHECK: error: operand must be a 16-bit value or a symbol
	sta	#$10

; Neither jmp nor jsr has a stack-relative form.
; CHECK: error: invalid operand for instruction
	jmp	$10,sp

; The read-modify-write group has no extended form, so its address operand has
; to fit in the direct page.
; CHECK: error: operand must be an 8-bit value
	neg	$1234

; Bit numbers are folded into the opcode and only go up to 7.
; CHECK: error: bit number must be in the range 0-7
	bset	8,$10

; A '#>' / '#<' byte selector yields a single byte, so it cannot supply the
; 16-bit immediate of ldhx.
; CHECK: error: operand must be a 16-bit value or a symbol
	ldhx	#>sym

; An 8-bit immediate that does not fit in a byte.
; CHECK: error: operand must be an 8-bit value
	lda	#$100

; A 16-bit immediate that does not fit in a word.
; CHECK: error: operand must be a 16-bit value or a symbol
	ldhx	#$10000

; An indexed displacement wider than 16 bits has no addressing mode. ($100,x
; would just widen from the 8-bit to the 16-bit displacement form.)
; CHECK: error: operand must be a 16-bit value or a symbol
	lda	$10000,x
