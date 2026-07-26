; REQUIRES: hcs08
; RUN: llvm-mc -filetype=obj -triple=hcs08 -motorola-integers %s -o %t.o
; RUN: ld.lld %t.o --defsym=far=0x1234 --defsym=dp=0x40 --image-base=0x8000 \
; RUN:   -Ttext=0x8000 -o %t
; RUN: llvm-objdump -s --section=.text %t | FileCheck %s

; A direct-page address is one byte, so a symbol that turns out not to be in
; the page is a link error rather than a silently truncated access. This is
; what the compiler relies on when it takes the user at their word about a
; variable being in .page0.
; RUN: not ld.lld %t.o --defsym=far=0x1234 --defsym=dp=0x1234 \
; RUN:   --image-base=0x8000 -Ttext=0x8000 -o /dev/null 2>&1 | \
; RUN:   FileCheck %s --check-prefix=ERR
; ERR: relocation R_HCS08_8 out of range: 4660 is not in [-128, 255]

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
  lda   <dp           ; R_HCS08_8, the direct-page form forced with '<'
back:
  bra   back          ; R_HCS08_PCREL_8, -2 from the end of the instruction
  bra   _start        ; -13 from the end of this one

; jmp $1234 / lda #$12 / lda #$34 / lda $40 / bra -2 / bra -13
; CHECK: 8000 cc1234a6 12a634b6 4020fe20 f3
