; RUN: llc -mtriple=hcs08 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=hcs08 -verify-machineinstrs -hcs08-dp-bank-size=4 < %s | \
; RUN:   FileCheck %s --check-prefix=BANK

; An interrupt handler is entered by the interrupt sequence rather than by a
; jsr, so it leaves with rti and has to put back what the hardware did not
; stack. See CodeGenDesign.md section 20.

@counter = global i8 0
@wide = global i16 0

declare void @helper()

; The sequence stacks the return address, X, A and the condition codes - but
; not H, which the CPU08 core added after that layout was fixed. H:X is this
; target's pointer register and its 16-bit accumulator, so a handler that does
; anything at all clobbers H, and dropping it corrupts the interrupted function
; rather than this one.
;
; CHECK-LABEL: bump:
; CHECK: pshh
; CHECK: pulh
; CHECK-NEXT: rti
define void @bump() "hcs08-interrupt" {
entry:
  %v = load volatile i8, ptr @counter
  %inc = add i8 %v, 1
  store volatile i8 %inc, ptr @counter
  ret void
}

; An ordinary function is untouched: no pshh, and it returns with rts.
;
; CHECK-LABEL: ordinary:
; CHECK-NOT: pshh
; CHECK: rts
define void @ordinary() {
entry:
  %v = load volatile i8, ptr @counter
  %inc = add i8 %v, 1
  store volatile i8 %inc, ptr @counter
  ret void
}

; With no bank there is nothing to preserve beyond H, whatever the handler
; does. With one, a leaf that allocated no bank slots still saves none of it -
; the count is exact because HCS08DirectPageBank does not run on handlers, so
; nothing can be taken after the prologue has been written.
;
; BANK-LABEL: leaf_nobank:
; BANK: pshh
; BANK-NOT: __hcs08_dp_bank
; BANK: pulh
; BANK-NEXT: rti
define void @leaf_nobank() "hcs08-interrupt" {
entry:
  %v = load volatile i8, ptr @counter
  %inc = add i8 %v, 1
  store volatile i8 %inc, ptr @counter
  ret void
}

; A call goes to code this function cannot see, which may be using the bank at
; the moment the interrupt lands, so a non-leaf handler saves all of it. The
; pushes come before the frame is allocated and the pops after it is freed:
; n,sp displacements are measured from where SP ends up, so a push in between
; would move SP out from under every one of them.
;
; BANK-LABEL: nonleaf:
; BANK: pshh
; BANK-NEXT: lda <__hcs08_dp_bank{{$}}
; BANK-NEXT: psha
; BANK-NEXT: lda <__hcs08_dp_bank+1
; BANK-NEXT: psha
; BANK-NEXT: lda <__hcs08_dp_bank+2
; BANK-NEXT: psha
; BANK-NEXT: lda <__hcs08_dp_bank+3
; BANK-NEXT: psha
; BANK: jsr helper
; Pops mirror pushes, so the bytes come back in the opposite order.
; BANK: pula
; BANK-NEXT: sta <__hcs08_dp_bank+3
; BANK-NEXT: pula
; BANK-NEXT: sta <__hcs08_dp_bank+2
; BANK-NEXT: pula
; BANK-NEXT: sta <__hcs08_dp_bank+1
; BANK-NEXT: pula
; BANK-NEXT: sta <__hcs08_dp_bank{{$}}
; BANK-NEXT: pulh
; BANK-NEXT: rti
define void @nonleaf() "hcs08-interrupt" {
entry:
  call void @helper()
  ret void
}

; no_direct_page_bank is a promise that nothing reachable from here touches the
; bank, so the save is dropped even though this one calls out. The half that is
; checkable is enforced rather than trusted: getHCS08DPBankSize reports zero for
; such a function, so the compiler cannot hand it a slot to contradict itself
; with.
;
; BANK-LABEL: promised:
; BANK: pshh
; BANK-NOT: __hcs08_dp_bank
; BANK: jsr helper
; BANK-NOT: __hcs08_dp_bank
; BANK: pulh
; BANK-NEXT: rti
define void @promised() "hcs08-interrupt" "hcs08-no-dp-bank" {
entry:
  call void @helper()
  ret void
}

; A leaf that does use the bank saves exactly the bytes it took, not the whole
; reservation: two parked 16-bit operands is four of the eight below.
;
; RUN: llc -mtriple=hcs08 -verify-machineinstrs -hcs08-dp-bank-size=8 < %s | \
; RUN:   FileCheck %s --check-prefix=PARTIAL
;
; PARTIAL-LABEL: leaf_usesbank:
; PARTIAL: pshh
; PARTIAL-COUNT-4: psha
; PARTIAL-NOT: psha
; PARTIAL: pulh
; PARTIAL-NEXT: rti
define void @leaf_usesbank() "hcs08-interrupt" {
entry:
  %a = load volatile i16, ptr @wide
  %b = load volatile i16, ptr @wide
  %sum = add i16 %a, %b
  %dif = sub i16 %a, %b
  %x = xor i16 %sum, %dif
  store volatile i16 %x, ptr @wide
  ret void
}
