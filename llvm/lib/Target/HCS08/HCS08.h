//===-- HCS08.h - Top-level interface for HCS08 ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HCS08_HCS08_H
#define LLVM_LIB_TARGET_HCS08_HCS08_H

#include "MCTargetDesc/HCS08MCTargetDesc.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {
class FunctionPass;
class HCS08TargetMachine;
class PassRegistry;

FunctionPass *createHCS08ISelDag(HCS08TargetMachine &TM,
                                 CodeGenOptLevel OptLevel);

void initializeHCS08DAGToDAGISelLegacyPass(PassRegistry &);
void initializeHCS08AsmPrinterPass(PassRegistry &);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HCS08_HCS08_H
