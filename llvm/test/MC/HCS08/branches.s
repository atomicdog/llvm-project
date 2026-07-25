; RUN: llvm-mc -triple=hcs08 -motorola-integers -show-encoding %s | FileCheck %s

; CHECK: bra target                          ; encoding: [0x20,A]
; CHECK: brn target                          ; encoding: [0x21,A]
; CHECK: bhi target                          ; encoding: [0x22,A]
; CHECK: bls target                          ; encoding: [0x23,A]
; CHECK: bcc target                          ; encoding: [0x24,A]
; CHECK: bcs target                          ; encoding: [0x25,A]
; CHECK: bne target                          ; encoding: [0x26,A]
; CHECK: beq target                          ; encoding: [0x27,A]
; CHECK: bhcc target                         ; encoding: [0x28,A]
; CHECK: bhcs target                         ; encoding: [0x29,A]
; CHECK: bpl target                          ; encoding: [0x2a,A]
; CHECK: bmi target                          ; encoding: [0x2b,A]
; CHECK: bmc target                          ; encoding: [0x2c,A]
; CHECK: bms target                          ; encoding: [0x2d,A]
; CHECK: bil target                          ; encoding: [0x2e,A]
; CHECK: bih target                          ; encoding: [0x2f,A]
; CHECK: bge target                          ; encoding: [0x90,A]
; CHECK: blt target                          ; encoding: [0x91,A]
; CHECK: bgt target                          ; encoding: [0x92,A]
; CHECK: ble target                          ; encoding: [0x93,A]
; CHECK: bsr target                          ; encoding: [0xad,A]
	bra	target
	brn	target
	bhi	target
	bls	target
	bcc	target
	bcs	target
	bne	target
	beq	target
	bhcc	target
	bhcs	target
	bpl	target
	bmi	target
	bmc	target
	bms	target
	bil	target
	bih	target
	bge	target
	blt	target
	bgt	target
	ble	target
	bsr	target

; The unsigned aliases share encodings with the carry-bit branches.
; CHECK: bcs target                          ; encoding: [0x25,A]
; CHECK: bcc target                          ; encoding: [0x24,A]
	blo	target
	bhs	target

; Compare/decrement-and-branch.
; CHECK: cbeq $20,target                     ; encoding: [0x31,0x20,A]
; CHECK: cbeqa #$20,target                   ; encoding: [0x41,0x20,A]
; CHECK: cbeqx #$20,target                   ; encoding: [0x51,0x20,A]
; CHECK: cbeq $20,x+,target                  ; encoding: [0x61,0x20,A]
; CHECK: cbeq ,x+,target                     ; encoding: [0x71,A]
; CHECK: cbeq $20,sp,target                  ; encoding: [0x9e,0x61,0x20,A]
	cbeq	$20,target
	cbeqa	#$20,target
	cbeqx	#$20,target
	cbeq	$20,x+,target
	cbeq	,x+,target
	cbeq	$20,sp,target

; CHECK: dbnz $20,target                     ; encoding: [0x3b,0x20,A]
; CHECK: dbnza target                        ; encoding: [0x4b,A]
; CHECK: dbnzx target                        ; encoding: [0x5b,A]
; CHECK: dbnz $20,x,target                   ; encoding: [0x6b,0x20,A]
; CHECK: dbnz ,x,target                      ; encoding: [0x7b,A]
; CHECK: dbnz $20,sp,target                  ; encoding: [0x9e,0x6b,0x20,A]
	dbnz	$20,target
	dbnza	target
	dbnzx	target
	dbnz	$20,x,target
	dbnz	,x,target
	dbnz	$20,sp,target

target:
	rts
