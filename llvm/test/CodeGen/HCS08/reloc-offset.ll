; RUN: llc -mtriple=hcs08 -filetype=obj < %s | llvm-readobj -r - | FileCheck %s

; A relocation has to point at the encoded field, which is not the same as the
; operand's index. A code-generation form carries register operands that
; contribute nothing to the encoding - "lda #$nn" is written with a destination
; register the assembler's own form does not have - so counting bytes by
; operand number puts the fixup one byte late. That went unnoticed for as long
; as everything went through the assembler, whose forms have no such operand;
; it only shows up in an object emitted directly, and then only once something
; applies the relocation.
;
; Every field below is the byte immediately after its opcode.

; CHECK:      0x1 R_HCS08_16 g
; CHECK-NEXT: 0x5 R_HCS08_16 g
; CHECK-NEXT: 0x9 R_HCS08_16 b
; CHECK-NEXT: 0xD R_HCS08_16 callee

@g = global i16 0
@b = global i8 0

define i16 @load_word() {
  %v = load i16, ptr @g
  ret i16 %v
}

define ptr @address_of() {
  ret ptr @g
}

define i8 @load_byte() {
  %v = load i8, ptr @b
  ret i8 %v
}

declare void @callee()

define void @call_it() {
  call void @callee()
  ret void
}
