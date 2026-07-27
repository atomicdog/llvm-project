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
  // The first byte-sized argument is passed in A and the first word-sized one
  // in H:X; anything past that goes on the stack, one slot per byte.
  CCIfType<[i8],  CCAssignToReg<[A]>>,
  CCIfType<[i16], CCAssignToReg<[HX]>>,
  CCIfType<[i8],  CCAssignToStack<1, 1>>,
  CCIfType<[i16], CCAssignToStack<2, 1>>
]>;

// A variadic function passes everything on the stack, named arguments
// included. va_arg walks the unnamed ones, so they have to be contiguous in
// memory and in the order they were written, which a register cannot be part
// of; and the alternative - named arguments in registers, unnamed ones on the
// stack - would have the two ends disagree about A whenever the first unnamed
// argument is byte-sized and no named one claimed it. Both ends know the
// function is variadic, because C requires the prototype to say so, so both
// pick this and there is nothing to get wrong.
//
// The cost is the fixed arguments of a variadic call, which is one word for
// the usual "int n, ...".
def CC_HCS08_VarArg : CallingConv<[
  CCIfType<[i1], CCPromoteToType<i8>>,
  CCIfType<[i8],  CCAssignToStack<1, 1>>,
  CCIfType<[i16], CCAssignToStack<2, 1>>
]>;

// Nothing is callee-saved yet (A / H:X are caller-saved).
def CSR_HCS08 : CalleeSavedRegs<(add)>;
