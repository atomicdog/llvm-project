; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=hcs08 -verify-machineinstrs -filetype=obj < %s -o %t.o
; RUN: llvm-readobj -S %t.o | FileCheck %s --check-prefix=SECTIONS

; The frame description exists for debuggers, not for unwinding: exceptions are
; off on this target and there is nothing to throw with. So without -g there is
; no reason to emit any of it, and nothing does - neither the directives nor
; the sections. A part with 32KB of flash should not carry unwind tables it has
; no unwinder for.
;
; Both a function with a frame and an interrupt handler, since the handler is
; the one that describes registers as well as the CFA.

; CHECK-NOT: .cfi_
; SECTIONS-NOT: Name: .debug_frame
; SECTIONS-NOT: Name: .eh_frame

define i16 @framed() {
entry:
  %buf = alloca [200 x i8], align 1
  store volatile i8 1, ptr %buf, align 1
  ret i16 0
}

define void @handler() #0 {
entry:
  %p = alloca i16, align 1
  store volatile i16 7, ptr %p, align 1
  ret void
}

attributes #0 = { "hcs08-interrupt" }
