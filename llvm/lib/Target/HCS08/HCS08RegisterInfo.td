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
def PC  : HCS08Reg<4, "pc">;
def SP  : HCS08Reg<5, "sp">;

// The condition codes, in the two groups that matter. They are halves of one
// architectural CCR, but which half an instruction writes is exactly what
// decides whether a sequence is correct:
//
//   NZV  N, Z and V. Written by almost everything that touches a value - a
//        load and a store included, which is why a compare and its branch
//        must stay adjacent.
//   C    the carry, and the half-carry that rides with it. Written only by
//        the arithmetic that can produce one, so it survives the loads and
//        stores that a multi-byte add has to interleave with.
//
// Modelling them as one register would make the second fact unsayable.
def NZV : HCS08Reg<3, "nzv">;
def C   : HCS08Reg<7, "c">;

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

def CCRC : RegisterClass<"HCS08", [i8], 8, (add NZV, C)> {
  let CopyCost = -1;
  let isAllocatable = 0;
}

// The base of an n,sp operand. It carries a frame index until
// eliminateFrameIndex rewrites it to SP, which is not a value register and is
// never allocated - so it needs a class of its own rather than GR16, which is
// what it used to claim to be.
def SPBASE : RegisterClass<"HCS08", [i16], 8, (add SP)> {
  let CopyCost = -1;
  let isAllocatable = 0;
}

// Not a register at all: a marker saying that the operand beside it is a byte
// offset into the direct-page bank rather than a displacement from SP. The
// 16-bit ALU pseudos take their operands from either place, and since they are
// expanded long after the difference stops being visible in a frame index,
// something on the instruction has to remember which. Naming it in the base
// operand keeps the (base, displacement) pair one shape.
def DPB : HCS08Reg<8, "dpb">;

def SLOTBASE : RegisterClass<"HCS08", [i16], 8, (add SP, DPB)> {
  let CopyCost = -1;
  let isAllocatable = 0;
}
