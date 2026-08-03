; RUN: llc -mtriple=hcs08 -O2 < %s | FileCheck %s
; RUN: llc -mtriple=hcs08 -O0 < %s | FileCheck %s

; A switch dense enough that the generic code would build a jump table for it.
;
; BR_JT and BRIND both default to Legal when a target says nothing about them,
; which makes areJTsAllowed() true and lets SelectionDAGBuilder pick a table.
; Selection then has no pattern for br_jt and dies with "Cannot select", a fatal
; error rather than bad code. Marking both Expand is what stops the table being
; formed at all.
;
; This machine has no indirect jump - the only way to a computed address is to
; push it and rts - so there is nothing to lower a table to even in principle.

define i16 @dense_switch(i8 %c) {
; CHECK-LABEL: dense_switch:
; A comparison chain, not a table. No jump-table section is emitted and no
; indirect jump appears.
; CHECK-NOT: .LJTI
; CHECK-NOT: jmp
; CHECK: rts
entry:
  switch i8 %c, label %other [
    i8 100, label %a
    i8 101, label %b
    i8 102, label %c1
    i8 103, label %d
    i8 104, label %e
    i8 105, label %f
    i8 106, label %g
    i8 107, label %h
    i8 108, label %i
    i8 109, label %j
  ]

a:     br label %out
b:     br label %out
c1:    br label %out
d:     br label %out
e:     br label %out
f:     br label %out
g:     br label %out
h:     br label %out
i:     br label %out
j:     br label %out
other: br label %out

out:
  %r = phi i16 [ 1, %a ], [ 2, %b ], [ 3, %c1 ], [ 4, %d ], [ 5, %e ],
               [ 6, %f ], [ 7, %g ], [ 8, %h ], [ 9, %i ], [ 10, %j ],
               [ 0, %other ]
  ret i16 %r
}

; The same shape a printf conversion switch has: sparse-ish characters over a
; range wide enough to tempt a table.
define i16 @conversion_switch(i8 %c) {
; CHECK-LABEL: conversion_switch:
; CHECK-NOT: .LJTI
; CHECK: rts
entry:
  switch i8 %c, label %def [
    i8 99,  label %L1   ; 'c'
    i8 100, label %L2   ; 'd'
    i8 105, label %L3   ; 'i'
    i8 111, label %L4   ; 'o'
    i8 112, label %L5   ; 'p'
    i8 115, label %L6   ; 's'
    i8 117, label %L7   ; 'u'
    i8 120, label %L8   ; 'x'
    i8 88,  label %L9   ; 'X'
  ]

L1:  br label %out
L2:  br label %out
L3:  br label %out
L4:  br label %out
L5:  br label %out
L6:  br label %out
L7:  br label %out
L8:  br label %out
L9:  br label %out
def: br label %out

out:
  %r = phi i16 [ 1, %L1 ], [ 2, %L2 ], [ 3, %L3 ], [ 4, %L4 ], [ 5, %L5 ],
               [ 6, %L6 ], [ 7, %L7 ], [ 8, %L8 ], [ 9, %L9 ], [ 0, %def ]
  ret i16 %r
}
