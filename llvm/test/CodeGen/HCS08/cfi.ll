; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=hcs08 -verify-machineinstrs -filetype=obj < %s -o %t.o
; RUN: llvm-dwarfdump --debug-frame %t.o | FileCheck %s --check-prefix=FRAME
; RUN: llvm-dwarfdump --verify %t.o | FileCheck %s --check-prefix=VERIFY

; VERIFY: No errors

; The CFA is SP as the caller had it at the call site. That is DWARF's own
; "typical" definition and the only one an unwinder gets for free: recovering
; the caller's SP is what lets the next frame's rule - also written against SP -
; be evaluated, and libunwind assigns the CFA to SP unconditionally rather than
; asking. See CodeGenDesign.md section 24.
;
; The return address therefore straddles the CFA instead of sitting below it:
; -1, not the -2 the same reasoning gives elsewhere. SP points one byte *below*
; the last thing pushed here, so jsr stores PCL at the CFA itself and PCH below
; it, and the two bytes read high-first start at CFA-1.
;
; FRAME:      Address size:          2
; FRAME:      Code alignment factor: 1
; FRAME:      Data alignment factor: -1
; FRAME:      Return address column: 5
; FRAME:      DW_CFA_def_cfa: SP +2
; FRAME-NEXT: DW_CFA_offset: PC -1

; An ordinary function only has to say where SP went. Each ais is described as
; it happens rather than once at the end, so the rule is true at every
; instruction boundary - which is the point, since a BDM probe halts wherever
; it halts.
;
; CHECK-LABEL: bump:
; CHECK:         .cfi_startproc
; CHECK:         ais #$fe
; CHECK-NEXT:    .cfi_def_cfa_offset 4
; CHECK:         ais #$02
; CHECK-NEXT:    .cfi_def_cfa_offset 2
; CHECK:         rts
define i16 @bump(i16 %n) !dbg !7 {
entry:
  %local = alloca i16, align 1
  %add = add nsw i16 %n, 1, !dbg !13
  store volatile i16 %add, ptr %local, align 1, !dbg !13
  %v = load volatile i16, ptr %local, align 1, !dbg !14
  ret i16 %v, !dbg !14
}

; A handler is entered with five bytes already pushed where a jsr pushes two,
; so it restates the distance and describes the three registers the hardware
; stacked. The offsets are measured, not read off the manual - frameprobe.py in
; the harness reads the frame from inside a handler. The condition codes at
; CFA-4 are the one byte with no rule: LLVM models the CCR as the two halves
; NZV and C, and neither names the whole register.
;
; CHECK-LABEL: handler:
; CHECK:         .cfi_def_cfa_offset 5
; CHECK-NEXT:    .cfi_offset x, -2
; CHECK-NEXT:    .cfi_offset a, -3
; CHECK:         pshh
; CHECK-NEXT:    .cfi_def_cfa_offset 6
; CHECK-NEXT:    .cfi_offset h, -5
; CHECK:         pulh
; CHECK-NEXT:    .cfi_def_cfa_offset 5
; CHECK-NEXT:    .cfi_restore h
; CHECK-NEXT:    rti
define void @handler() #0 !dbg !15 {
entry:
  %p = alloca i16, align 1
  store volatile i16 7, ptr %p, align 1, !dbg !16
  ret void, !dbg !16
}

; Two returns means two epilogues, and the block laid out after the first would
; otherwise inherit its rules while the frame is still up. The CFI fixup pass,
; which the target opts into for this reason, brackets the range instead.
;
; CHECK-LABEL: early:
; CHECK:         .cfi_def_cfa_offset 4
; CHECK-NEXT:    .cfi_remember_state
; CHECK:         .cfi_def_cfa_offset 2
; CHECK:         .cfi_restore_state
define i16 @early(i16 %n) !dbg !17 {
entry:
  %s = alloca i16, align 1
  %c = icmp slt i16 %n, 0, !dbg !18
  br i1 %c, label %neg, label %pos
neg:
  ret i16 0, !dbg !18
pos:
  store volatile i16 %n, ptr %s, align 1, !dbg !18
  %v = load volatile i16, ptr %s, align 1, !dbg !18
  ret i16 %v, !dbg !18
}

attributes #0 = { "hcs08-interrupt" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "cfi.c", directory: "/tmp")
!3 = !{i32 7, !"Dwarf Version", i32 5}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!7 = distinct !DISubprogram(name: "bump", scope: !1, file: !1, line: 1, type: !8, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0)
!8 = !DISubroutineType(types: !9)
!9 = !{!10, !10}
!10 = !DIBasicType(name: "int", size: 16, encoding: DW_ATE_signed)
!13 = !DILocation(line: 2, column: 7, scope: !7)
!14 = !DILocation(line: 3, column: 3, scope: !7)
!15 = distinct !DISubprogram(name: "handler", scope: !1, file: !1, line: 6, type: !8, scopeLine: 6, spFlags: DISPFlagDefinition, unit: !0)
!16 = !DILocation(line: 7, column: 3, scope: !15)
!17 = distinct !DISubprogram(name: "early", scope: !1, file: !1, line: 10, type: !8, scopeLine: 10, spFlags: DISPFlagDefinition, unit: !0)
!18 = !DILocation(line: 11, column: 3, scope: !17)
