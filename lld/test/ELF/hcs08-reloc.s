; REQUIRES: hcs08
; RUN: llvm-mc -filetype=obj -triple=hcs08 -motorola-integers %s -o %t.o
; RUN: ld.lld %t.o --defsym=far=0x1234 --image-base=0x8000 -Ttext=0x8000 -o %t
; RUN: llvm-objdump -s --section=.text %t | FileCheck %s

; The HCS08 relocation set is LLVM-defined: an absolute byte, an absolute
; big-endian word, a signed byte of branch displacement measured from the end
; of the instruction, and the two halves of an address for materializing it
; through the 8-bit accumulator. ";" is the comment character here because "#"
; introduces an immediate.

  .text
  .globl _start
_start:
  jmp   far           ; R_HCS08_16, big endian
  lda   #>far         ; R_HCS08_HI8
  lda   #<far         ; R_HCS08_LO8
back:
  bra   back          ; R_HCS08_PCREL_8, -2 from the end of the instruction
  bra   _start        ; -11 from the end of this one

; jmp $1234 / lda #$12 / lda #$34 / bra -2 / bra -11
; CHECK: 8000 cc1234a6 12a63420 fe20f5
