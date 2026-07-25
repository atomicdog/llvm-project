; RUN: llvm-mc -triple=hcs08 -motorola-integers -show-encoding %s | FileCheck %s

; Read-modify-write group: row 3 is direct, 4 and 5 operate on A and X, 6 and 7
; are the indexed modes, and page-2 row 6 is stack-relative.

; CHECK: neg $40                             ; encoding: [0x30,0x40]
; CHECK: nega                                ; encoding: [0x40]
; CHECK: negx                                ; encoding: [0x50]
; CHECK: neg $41,x                           ; encoding: [0x60,0x41]
; CHECK: neg ,x                              ; encoding: [0x70]
; CHECK: neg $42,sp                          ; encoding: [0x9e,0x60,0x42]
	neg	$40
	nega
	negx
	neg	$41,x
	neg	,x
	neg	$42,sp

; CHECK: com $10                             ; encoding: [0x33,0x10]
; CHECK: lsr $10                             ; encoding: [0x34,0x10]
; CHECK: ror $10                             ; encoding: [0x36,0x10]
; CHECK: asr $10                             ; encoding: [0x37,0x10]
; CHECK: lsl $10                             ; encoding: [0x38,0x10]
; CHECK: rol $10                             ; encoding: [0x39,0x10]
; CHECK: dec $10                             ; encoding: [0x3a,0x10]
; CHECK: inc $10                             ; encoding: [0x3c,0x10]
; CHECK: tst $10                             ; encoding: [0x3d,0x10]
; CHECK: clr $10                             ; encoding: [0x3f,0x10]
	com	$10
	lsr	$10
	ror	$10
	asr	$10
	lsl	$10
	rol	$10
	dec	$10
	inc	$10
	tst	$10
	clr	$10

; CHECK: coma                                ; encoding: [0x43]
; CHECK: lsra                                ; encoding: [0x44]
; CHECK: clra                                ; encoding: [0x4f]
; CHECK: comx                                ; encoding: [0x53]
; CHECK: clrx                                ; encoding: [0x5f]
	coma
	lsra
	clra
	comx
	clrx

; "asl" is the traditional spelling of "lsl" and encodes identically.
; CHECK: lsl $10                             ; encoding: [0x38,0x10]
; CHECK: lsla                                ; encoding: [0x48]
	asl	$10
	asla
