//===-- HCS08RegisterInfo.td - HCS08 Register defs -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declarations that describe the HCS08 register file.
//
// The HCS08 programmer's model is tiny: an 8-bit accumulator A, a 16-bit index
// register H:X addressable as two 8-bit halves, a 16-bit stack pointer, the
// program counter, and the condition code register. The index and stack
// registers appear in assembly only as part of the addressing-mode syntax
// (",x", "n,sp"), not as general operands, so for the MC layer these
// definitions exist mainly so that MCRegisterInfo is populated.
//
//===----------------------------------------------------------------------===//

class HCS08Reg<bits<16> Enc, string n, list<string> alt = []> : Register<n> {
  let HWEncoding = Enc;
  let Namespace = "HCS08";
  let AltNames = alt;
}

class HCS08RegWithSubRegs<bits<16> Enc, string n, list<Register> subregs>
    : RegisterWithSubRegs<n, subregs> {
  let HWEncoding = Enc;
  let Namespace = "HCS08";
}

let Namespace = "HCS08" in {
  def sub_hi : SubRegIndex<8, 8>;
  def sub_lo : SubRegIndex<8, 0>;
}

def A   : HCS08Reg<0, "a">;
def H   : HCS08Reg<1, "h">;
def X   : HCS08Reg<2, "x">;
def CCR : HCS08Reg<3, "ccr">;
def PC  : HCS08Reg<4, "pc">;
def SP  : HCS08Reg<5, "sp">;

// The index register is the H:X pair. H is the high byte.
let SubRegIndices = [sub_hi, sub_lo] in
def HX : HCS08RegWithSubRegs<6, "hx", [H, X]>;

//===----------------------------------------------------------------------===//
// Register classes
//===----------------------------------------------------------------------===//

// Everything is byte aligned on this target; there are no alignment
// requirements on any access.
//
// Model A (see CodeGenDesign.md): the only allocatable 8-bit register is the
// accumulator A, and the only allocatable 16-bit register is the index pair
// H:X. SP is reserved. X and H remain defined as the halves of H:X but are not
// independently allocatable yet.
def GR8  : RegisterClass<"HCS08", [i8],  8, (add A)>;
def GR16 : RegisterClass<"HCS08", [i16], 8, (add HX)>;

def CCRC : RegisterClass<"HCS08", [i8], 8, (add CCR)> {
  let CopyCost = -1;
  let isAllocatable = 0;
}
