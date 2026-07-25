; RUN: llvm-mc -triple=hcs08 -motorola-integers -show-encoding %s | FileCheck %s

; Direct-page bit manipulation. The bit number is folded into the opcode: bset
; and bclr occupy 0x10-0x1f, brset and brclr occupy 0x00-0x0f, with the bit
; number in bits 3-1 and set/clear in bit 0.

; CHECK: bset 0,$10                          ; encoding: [0x10,0x10]
; CHECK: bclr 0,$10                          ; encoding: [0x11,0x10]
; CHECK: bset 3,$10                          ; encoding: [0x16,0x10]
; CHECK: bclr 3,$10                          ; encoding: [0x17,0x10]
; CHECK: bset 7,$10                          ; encoding: [0x1e,0x10]
; CHECK: bclr 7,$10                          ; encoding: [0x1f,0x10]
	bset	0,$10
	bclr	0,$10
	bset	3,$10
	bclr	3,$10
	bset	7,$10
	bclr	7,$10

; CHECK: brset 0,$10,target                  ; encoding: [0x00,0x10,A]
; CHECK: brclr 0,$10,target                  ; encoding: [0x01,0x10,A]
; CHECK: brset 5,$10,target                  ; encoding: [0x0a,0x10,A]
; CHECK: brclr 7,$10,target                  ; encoding: [0x0f,0x10,A]
	brset	0,$10,target
	brclr	0,$10,target
	brset	5,$10,target
	brclr	7,$10,target
target:
	rts
