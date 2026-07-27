; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s

; f32 is softened all the way to libcalls: there is no FPU and nothing in the
; target marks any float operation as anything but expand, so what this checks
; is that every one of them reaches the runtime under the name compiler-rt
; defines, with nothing quietly left legal along the way.
;
; The behaviour of the routines themselves is verified on the simulator - see
; f32matrix.py in the verification harness - because a name is all that can be
; checked here.

define float @add(float %a, float %b) {
; CHECK-LABEL: add:
; CHECK: jsr __addsf3
  %r = fadd float %a, %b
  ret float %r
}

define float @sub(float %a, float %b) {
; CHECK-LABEL: sub:
; CHECK: jsr __subsf3
  %r = fsub float %a, %b
  ret float %r
}

define float @mul(float %a, float %b) {
; CHECK-LABEL: mul:
; CHECK: jsr __mulsf3
  %r = fmul float %a, %b
  ret float %r
}

define float @div(float %a, float %b) {
; CHECK-LABEL: div:
; CHECK: jsr __divsf3
  %r = fdiv float %a, %b
  ret float %r
}

; Negation is a sign-bit flip, not a call: there is no __negsf2 here.
define float @neg(float %a) {
; CHECK-LABEL: neg:
; CHECK-NOT: jsr __negsf2
  %r = fneg float %a
  ret float %r
}

; Comparisons. compiler-rt implements __lesf2 and __gesf2 and makes the rest
; aliases of those two, so which of the six names appears is up to the
; expansion; what matters is that an ordered predicate becomes one of them and
; an unordered one reaches __unordsf2.
define i1 @cmp_olt(float %a, float %b) {
; CHECK-LABEL: cmp_olt:
; CHECK: jsr __{{(lt|ge)}}sf2
  %r = fcmp olt float %a, %b
  ret i1 %r
}

define i1 @cmp_oeq(float %a, float %b) {
; CHECK-LABEL: cmp_oeq:
; CHECK: jsr __{{(eq|ne)}}sf2
  %r = fcmp oeq float %a, %b
  ret i1 %r
}

define i1 @cmp_uno(float %a, float %b) {
; CHECK-LABEL: cmp_uno:
; CHECK: jsr __unordsf2
  %r = fcmp uno float %a, %b
  ret i1 %r
}

; Conversions. int is 16 bits here, so an i16 widens to i32 and goes through
; the same __floatsisf as a long rather than wanting a routine of its own.
; The widening of the signed one is in sext-widen.ll.
define float @from_i16(i16 %a) {
; CHECK-LABEL: from_i16:
; CHECK: jsr __floatsisf
  %r = sitofp i16 %a to float
  ret float %r
}

define float @from_u16(i16 %a) {
; CHECK-LABEL: from_u16:
; CHECK: jsr __floatunsisf
  %r = uitofp i16 %a to float
  ret float %r
}

define float @from_i32(i32 %a) {
; CHECK-LABEL: from_i32:
; CHECK: jsr __floatsisf
  %r = sitofp i32 %a to float
  ret float %r
}

define float @from_u32(i32 %a) {
; CHECK-LABEL: from_u32:
; CHECK: jsr __floatunsisf
  %r = uitofp i32 %a to float
  ret float %r
}

define float @from_i64(i64 %a) {
; CHECK-LABEL: from_i64:
; CHECK: jsr __floatdisf
  %r = sitofp i64 %a to float
  ret float %r
}

define float @from_u64(i64 %a) {
; CHECK-LABEL: from_u64:
; CHECK: jsr __floatundisf
  %r = uitofp i64 %a to float
  ret float %r
}

define i32 @to_i32(float %a) {
; CHECK-LABEL: to_i32:
; CHECK: jsr __fixsfsi
  %r = fptosi float %a to i32
  ret i32 %r
}

define i32 @to_u32(float %a) {
; CHECK-LABEL: to_u32:
; CHECK: jsr __fixunssfsi
  %r = fptoui float %a to i32
  ret i32 %r
}

define i64 @to_i64(float %a) {
; CHECK-LABEL: to_i64:
; CHECK: jsr __fixsfdi
  %r = fptosi float %a to i64
  ret i64 %r
}

define i64 @to_u64(float %a) {
; CHECK-LABEL: to_u64:
; CHECK: jsr __fixunssfdi
  %r = fptoui float %a to i64
  ret i64 %r
}
