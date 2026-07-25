//===-- HCS08CallingConv.td - Calling Conventions for HCS08 *- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the LLVM-defined HCS08 calling convention. There is no published
// HCS08 psABI; see CodeGenDesign.md. Bring-up convention: values returned in
// A (i8) / H:X (i16), arguments passed on the stack.
//
//===----------------------------------------------------------------------===//

def RetCC_HCS08 : CallingConv<[
  CCIfType<[i1], CCPromoteToType<i8>>,
  CCIfType<[i8],  CCAssignToReg<[A]>>,
  CCIfType<[i16], CCAssignToReg<[HX]>>
]>;

def CC_HCS08 : CallingConv<[
  CCIfType<[i1], CCPromoteToType<i8>>,
  // Bring-up: everything on the stack, one slot per byte, byte aligned.
  CCIfType<[i8],  CCAssignToStack<1, 1>>,
  CCIfType<[i16], CCAssignToStack<2, 1>>
]>;

// Nothing is callee-saved yet (A / H:X are caller-saved).
def CSR_HCS08 : CalleeSavedRegs<(add)>;
