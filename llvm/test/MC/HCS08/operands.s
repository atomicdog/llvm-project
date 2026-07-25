; RUN: llvm-mc -triple=hcs08 -motorola-integers -show-encoding %s | FileCheck %s

; Operand syntax that is independent of any particular instruction: literal
; bases, letter case, signed immediates, and the direct-vs-extended choice.
; Immediates always print back in the traditional Freescale "$nn" hex form.

; The same byte can be written in hex, decimal or binary.
; CHECK: lda #$aa                            ; encoding: [0xa6,0xaa]
; CHECK: lda #$aa                            ; encoding: [0xa6,0xaa]
; CHECK: lda #$aa                            ; encoding: [0xa6,0xaa]
	lda	#$aa
	lda	#170
	lda	#%10101010

; Mnemonics and register names are case-insensitive.
; CHECK: lda #$11                            ; encoding: [0xa6,0x11]
; CHECK: lda ,x                              ; encoding: [0xf6]
; CHECK: lda $10,sp                          ; encoding: [0x9e,0xe6,0x10]
	LDA	#$11
	lda	,X
	lda	$10,SP

; ais and aix take a signed byte.
; CHECK: ais #$ff                            ; encoding: [0xa7,0xff]
; CHECK: aix #$fc                            ; encoding: [0xaf,0xfc]
	ais	#-1
	aix	#-4

; A value that fits in the direct page selects the 2-byte direct form; a wider
; value selects the 3-byte extended form. The choice is made from the value,
; not the spelling.
; CHECK: lda $ff                             ; encoding: [0xb6,0xff]
; CHECK: lda $0100                           ; encoding: [0xc6,0x01,0x00]
	lda	$ff
	lda	$100

; A symbol whose value is unknown always takes the extended form.
; CHECK: lda far                             ; encoding: [0xc6,A,A]
	lda	far
