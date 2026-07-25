; RUN: llvm-mc -triple=hcs08 -motorola-integers -show-encoding %s | FileCheck %s

; CHECK: mul                                 ; encoding: [0x42]
; CHECK: div                                 ; encoding: [0x52]
; CHECK: nsa                                 ; encoding: [0x62]
; CHECK: daa                                 ; encoding: [0x72]
	mul
	div
	nsa
	daa

; CHECK: rti                                 ; encoding: [0x80]
; CHECK: rts                                 ; encoding: [0x81]
; CHECK: bgnd                                ; encoding: [0x82]
; CHECK: swi                                 ; encoding: [0x83]
; CHECK: tap                                 ; encoding: [0x84]
; CHECK: tpa                                 ; encoding: [0x85]
; CHECK: pula                                ; encoding: [0x86]
; CHECK: psha                                ; encoding: [0x87]
; CHECK: pulx                                ; encoding: [0x88]
; CHECK: pshx                                ; encoding: [0x89]
; CHECK: pulh                                ; encoding: [0x8a]
; CHECK: pshh                                ; encoding: [0x8b]
; CHECK: clrh                                ; encoding: [0x8c]
; CHECK: stop                                ; encoding: [0x8e]
; CHECK: wait                                ; encoding: [0x8f]
	rti
	rts
	bgnd
	swi
	tap
	tpa
	pula
	psha
	pulx
	pshx
	pulh
	pshh
	clrh
	stop
	wait

; CHECK: txs                                 ; encoding: [0x94]
; CHECK: tsx                                 ; encoding: [0x95]
; CHECK: tax                                 ; encoding: [0x97]
; CHECK: clc                                 ; encoding: [0x98]
; CHECK: sec                                 ; encoding: [0x99]
; CHECK: cli                                 ; encoding: [0x9a]
; CHECK: sei                                 ; encoding: [0x9b]
; CHECK: rsp                                 ; encoding: [0x9c]
; CHECK: nop                                 ; encoding: [0x9d]
; CHECK: txa                                 ; encoding: [0x9f]
	txs
	tsx
	tax
	clc
	sec
	cli
	sei
	rsp
	nop
	txa
