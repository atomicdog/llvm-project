//===--- BuiltinsHCS08.td - HCS08 Builtin function database -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The HCS08-specific builtin function database. Each of these names an
// instruction that acts on the CPU rather than on a value, so there is nothing
// C can say instead. What they mean is documented on the intrinsics they map
// to, in llvm/include/llvm/IR/IntrinsicsHCS08.td; the mapping itself is the
// ClangBuiltin<> on each of those, so nothing here needs CodeGen support.
//
//===----------------------------------------------------------------------===//

include "clang/Basic/BuiltinsBase.td"

class HCS08Builtin<string prototype> : TargetBuiltin {
  let Prototype = prototype;
}

// Interrupt masking. I is a mask, so sei disables and cli enables - the
// opposite of what the mnemonics suggest elsewhere.
def SEI : HCS08Builtin<"void()"> { let Spellings = ["__builtin_hcs08_sei"]; }
def CLI : HCS08Builtin<"void()"> { let Spellings = ["__builtin_hcs08_cli"]; }

// The condition code register, for critical sections that nest.
def GetCCR : HCS08Builtin<"unsigned char()"> {
  let Spellings = ["__builtin_hcs08_get_ccr"];
}
def SetCCR : HCS08Builtin<"void(unsigned char)"> {
  let Spellings = ["__builtin_hcs08_set_ccr"];
}

// Low power until an interrupt arrives. stop is deeper than wait and is
// disabled out of reset on most parts.
def Wait : HCS08Builtin<"void()"> { let Spellings = ["__builtin_hcs08_wait"]; }
def Stop : HCS08Builtin<"void()"> { let Spellings = ["__builtin_hcs08_stop"]; }

// One cycle of nothing, for padding a timing loop.
def Nop : HCS08Builtin<"void()"> { let Spellings = ["__builtin_hcs08_nop"]; }
