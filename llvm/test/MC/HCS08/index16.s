; RUN: llvm-mc -triple=hcs08 -motorola-integers -show-encoding %s | FileCheck %s

; The 16-bit index register instructions and the memory-to-memory move. These
; are HCS08 additions; the indexed and stack-relative forms live on page 2.

; CHECK: ldhx #$1234                         ; encoding: [0x45,0x12,0x34]
; CHECK: ldhx $10                            ; encoding: [0x55,0x10]
; CHECK: ldhx $1234                          ; encoding: [0x32,0x12,0x34]
; CHECK: ldhx ,x                             ; encoding: [0x9e,0xae]
; CHECK: ldhx $1234,x                        ; encoding: [0x9e,0xbe,0x12,0x34]
; CHECK: ldhx $10,x                          ; encoding: [0x9e,0xce,0x10]
; CHECK: ldhx $10,sp                         ; encoding: [0x9e,0xfe,0x10]
	ldhx	#$1234
	ldhx	$10
	ldhx	$1234
	ldhx	,x
	ldhx	$1234,x
	ldhx	$10,x
	ldhx	$10,sp

; CHECK: sthx $10                            ; encoding: [0x35,0x10]
; CHECK: sthx $1234                          ; encoding: [0x96,0x12,0x34]
; CHECK: sthx $10,sp                         ; encoding: [0x9e,0xff,0x10]
	sthx	$10
	sthx	$1234
	sthx	$10,sp

; CHECK: cphx #$1234                         ; encoding: [0x65,0x12,0x34]
; CHECK: cphx $10                            ; encoding: [0x75,0x10]
; CHECK: cphx $1234                          ; encoding: [0x3e,0x12,0x34]
; CHECK: cphx $10,sp                         ; encoding: [0x9e,0xf3,0x10]
	cphx	#$1234
	cphx	$10
	cphx	$1234
	cphx	$10,sp

; CHECK: mov $20,$21                         ; encoding: [0x4e,0x20,0x21]
; CHECK: mov $30,x+                          ; encoding: [0x5e,0x30]
; CHECK: mov #$aa,$20                        ; encoding: [0x6e,0xaa,0x20]
; CHECK: mov ,x+,$30                         ; encoding: [0x7e,0x30]
	mov	$20,$21
	mov	$30,x+
	mov	#$aa,$20
	mov	,x+,$30
