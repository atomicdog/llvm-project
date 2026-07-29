; RUN: llc -mtriple=hcs08 -verify-machineinstrs -filetype=obj < %s -o %t.o
; RUN: llvm-dwarfdump --verify %t.o | FileCheck %s --check-prefix=VERIFY
; RUN: llvm-dwarfdump --debug-info %t.o | FileCheck %s

; VERIFY: No errors

; Frame base is SP, which needs a DWARF number - the target defined none at
; all, so locations came out as a bare DW_OP_plus_uconst with nothing to be
; relative to. See CodeGenDesign.md section 23.
;
; CHECK: DW_AT_frame_base ({{.*}}DW_OP_reg4 SP)

; The offset is the one eliminateFrameIndex computes, +1 included: SP points one
; byte below the last thing pushed, so the lowest byte of the frame is 1,sp.
; Inheriting the default frame reference, which stops at object offset plus
; frame size, described every local one byte low - which a debugger reads as the
; high half of one variable joined to the low half of its neighbour, and prints
; as a plausible wrong number.
;
; CHECK: DW_TAG_variable
; CHECK-NEXT: DW_AT_location (DW_OP_fbreg +1)
; CHECK-NEXT: DW_AT_name ("local")

define i16 @bump(i16 %n) !dbg !7 {
entry:
  %local = alloca i16, align 1
    #dbg_declare(ptr %local, !12, !DIExpression(), !13)
  %add = add nsw i16 %n, 1, !dbg !13
  store volatile i16 %add, ptr %local, align 1, !dbg !13
  %v = load volatile i16, ptr %local, align 1, !dbg !14
  ret i16 %v, !dbg !14
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "dbg.c", directory: "/tmp")
!3 = !{i32 7, !"Dwarf Version", i32 5}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!7 = distinct !DISubprogram(name: "bump", scope: !1, file: !1, line: 1, type: !8, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0)
!8 = !DISubroutineType(types: !9)
!9 = !{!10, !10}
!10 = !DIBasicType(name: "int", size: 16, encoding: DW_ATE_signed)
!12 = !DILocalVariable(name: "local", scope: !7, file: !1, line: 2, type: !10)
!13 = !DILocation(line: 2, column: 7, scope: !7)
!14 = !DILocation(line: 3, column: 3, scope: !7)
