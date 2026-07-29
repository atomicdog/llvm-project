; RUN: llvm-mc -triple=hcs08 -motorola-integers -filetype=obj %s | \
; RUN:   llvm-readobj -r - | FileCheck %s
; RUN: llvm-mc -triple=hcs08 -motorola-integers -filetype=obj %s | \
; RUN:   llvm-objdump -s -j .data - | FileCheck %s --check-prefix=DATA

; Nothing on this machine is 32 bits wide, so R_HCS08_32 is not an address
; relocation. It exists because 32-bit DWARF refers between its own sections
; with four-byte offsets whatever the target's pointer size, and without it
; every -g compilation aborted in the ELF writer's "invalid fixup kind".

	.data
	.globl target
target:
	.byte 0

; CHECK:      Relocations [
; CHECK:        Section {{.*}} .rela.data {
; CHECK-NEXT:     0x1 R_HCS08_8 target 0x0
; CHECK-NEXT:     0x2 R_HCS08_16 target 0x0
; CHECK-NEXT:     0x4 R_HCS08_32 target 0x0
	.byte target
	.short target
	.long target

; Big endian, like every other multi-byte field here: the four zero bytes are
; the placeholder the relocation will overwrite, most significant first.
; DATA: 0000 00000000 0000
