//===-- HCS08TargetInfo.cpp - HCS08 Target Implementation ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/HCS08TargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

Target &llvm::getTheHCS08Target() {
  static Target TheHCS08Target;
  return TheHCS08Target;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeHCS08TargetInfo() {
  RegisterTarget<Triple::hcs08, /*HasJIT=*/false> X(
      getTheHCS08Target(), "hcs08", "NXP (Freescale) HCS08 / MC9S08", "HCS08");
}
