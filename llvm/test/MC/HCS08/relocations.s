; RUN: llvm-mc -triple=hcs08 -motorola-integers -filetype=obj %s -o %t.o
; RUN: llvm-readobj --file-headers -r -x .text %t.o | FileCheck %s

; HCS08 objects use EM_68HC08, the machine number of the HC08 family this
; core descends from.
; CHECK: Machine: EM_68HC08 (0x47)

_start:
; A symbol whose value is not known selects the extended form and needs a
; 16-bit relocation.
	lda	far
; A constant that fits in a byte keeps the direct-page form and needs none.
	sta	$40
; Branches within the section resolve at assembly time. The displacement is
; measured from the end of the instruction: this bra sits at offset 5 and ends
; at 7, so a target at 0 encodes as -7 = $f9.
	bra	_start
; Data directives use the generic fixups, which map onto the same relocations.
	.byte	bytesym
	.2byte	wordsym

; CHECK:      Relocations [
; CHECK-NEXT:   Section (3) .rela.text {
; CHECK-NEXT:     0x1 R_HCS08_16 far 0x0
; CHECK-NEXT:     0x7 R_HCS08_8 bytesym 0x0
; CHECK-NEXT:     0x8 R_HCS08_16 wordsym 0x0
; CHECK-NEXT:   }
; CHECK-NEXT: ]

; CHECK: 0x00000000 c60000b7 4020f900 0000
