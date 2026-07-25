; RUN: llvm-mc -triple=hcs08 -motorola-integers -show-encoding %s | FileCheck %s

; The load/store/arithmetic instructions form a regular matrix: the low nibble
; of the opcode selects the operation and the high nibble selects the
; addressing mode. Page 2 (the 0x9E prefix) reuses rows D and E for the
; stack-relative modes.

; CHECK: sub #$11                            ; encoding: [0xa0,0x11]
; CHECK: sub $22                             ; encoding: [0xb0,0x22]
; CHECK: sub $3344                           ; encoding: [0xc0,0x33,0x44]
; CHECK: sub $5566,x                         ; encoding: [0xd0,0x55,0x66]
; CHECK: sub $77,x                           ; encoding: [0xe0,0x77]
; CHECK: sub ,x                              ; encoding: [0xf0]
; CHECK: sub $88,sp                          ; encoding: [0x9e,0xe0,0x88]
; CHECK: sub $99aa,sp                        ; encoding: [0x9e,0xd0,0x99,0xaa]
	sub	#$11
	sub	$22
	sub	$3344
	sub	$5566,x
	sub	$77,x
	sub	,x
	sub	$88,sp
	sub	$99aa,sp

; CHECK: cmp #$11                            ; encoding: [0xa1,0x11]
; CHECK: sbc #$11                            ; encoding: [0xa2,0x11]
; CHECK: cpx #$11                            ; encoding: [0xa3,0x11]
; CHECK: and #$11                            ; encoding: [0xa4,0x11]
; CHECK: bit #$11                            ; encoding: [0xa5,0x11]
; CHECK: lda #$11                            ; encoding: [0xa6,0x11]
; CHECK: eor #$11                            ; encoding: [0xa8,0x11]
; CHECK: adc #$11                            ; encoding: [0xa9,0x11]
; CHECK: ora #$11                            ; encoding: [0xaa,0x11]
; CHECK: add #$11                            ; encoding: [0xab,0x11]
; CHECK: ldx #$11                            ; encoding: [0xae,0x11]
	cmp	#$11
	sbc	#$11
	cpx	#$11
	and	#$11
	bit	#$11
	lda	#$11
	eor	#$11
	adc	#$11
	ora	#$11
	add	#$11
	ldx	#$11

; Stores have no immediate form; the immediate row uses those two columns for
; the "add immediate to pointer" instructions instead.
; CHECK: sta $22                             ; encoding: [0xb7,0x22]
; CHECK: sta $3344                           ; encoding: [0xc7,0x33,0x44]
; CHECK: sta $5566,x                         ; encoding: [0xd7,0x55,0x66]
; CHECK: sta $77,x                           ; encoding: [0xe7,0x77]
; CHECK: sta ,x                              ; encoding: [0xf7]
; CHECK: sta $88,sp                          ; encoding: [0x9e,0xe7,0x88]
; CHECK: sta $99aa,sp                        ; encoding: [0x9e,0xd7,0x99,0xaa]
; CHECK: stx $22                             ; encoding: [0xbf,0x22]
; CHECK: stx ,x                              ; encoding: [0xff]
	sta	$22
	sta	$3344
	sta	$5566,x
	sta	$77,x
	sta	,x
	sta	$88,sp
	sta	$99aa,sp
	stx	$22
	stx	,x

; CHECK: ais #$fc                            ; encoding: [0xa7,0xfc]
; CHECK: aix #$02                            ; encoding: [0xaf,0x02]
	ais	#$fc
	aix	#$02

; jmp and jsr have no immediate or stack-relative forms.
; CHECK: jmp $22                             ; encoding: [0xbc,0x22]
; CHECK: jmp $3344                           ; encoding: [0xcc,0x33,0x44]
; CHECK: jmp $5566,x                         ; encoding: [0xdc,0x55,0x66]
; CHECK: jmp $77,x                           ; encoding: [0xec,0x77]
; CHECK: jmp ,x                              ; encoding: [0xfc]
; CHECK: jsr $22                             ; encoding: [0xbd,0x22]
; CHECK: jsr $3344                           ; encoding: [0xcd,0x33,0x44]
; CHECK: jsr ,x                              ; encoding: [0xfd]
	jmp	$22
	jmp	$3344
	jmp	$5566,x
	jmp	$77,x
	jmp	,x
	jsr	$22
	jsr	$3344
	jsr	,x
