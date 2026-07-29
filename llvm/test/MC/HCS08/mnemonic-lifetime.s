; RUN: llvm-mc -triple=hcs08 -motorola-integers -filetype=obj -g -o /dev/null %s
; RUN: llvm-mc -triple=hcs08 -motorola-integers -filetype=obj -o /dev/null %s

; The mnemonic is lower-cased into storage that has to outlive parseInstruction:
; HCS08Operand holds the token as a StringRef and does not own it, and the
; matcher does not run until matchAndEmitInstruction, after parseInstruction has
; returned. A local std::string there left every mnemonic pointing at freed
; stack, which read the right bytes often enough to look correct.
;
; -g is what exposed it - it does enough work between the parse and the match to
; disturb the stack - and the symptom was that *every* mnemonic in the file
; became "invalid instruction mnemonic", operands or not. So this test is the
; same input twice, once each way; the -g line is the one that used to fail.

	.text
	.globl _start
_start:
	ldhx	#$0800
	txs
	lda	#$01
	sta	$80
	tax
	rts
