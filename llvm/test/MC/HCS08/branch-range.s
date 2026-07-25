; RUN: not llvm-mc -triple=hcs08 -motorola-integers -filetype=obj %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s

; A branch displacement is a signed byte measured from the end of the
; instruction, so a target more than ~127 bytes away cannot be reached. The
; check runs when the displacement is resolved, i.e. at object emission, and
; applies to every instruction that carries a branch operand.

; CHECK: error: branch target out of range
	bra	fwd
; CHECK: error: branch target out of range
	brset	0,$10,fwd
; CHECK: error: branch target out of range
	cbeq	$20,fwd
; CHECK: error: branch target out of range
	dbnz	$20,fwd
	.zero	200
fwd:
	rts

; The same limit applies to backward branches.
back:
	rts
	.zero	200
; CHECK: error: branch target out of range
	bra	back
