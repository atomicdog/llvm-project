//===-- HCS08TargetMachine.cpp - Define TargetMachine for HCS08 ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HCS08TargetMachine.h"
#include "HCS08.h"
#include "TargetInfo/HCS08TargetInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  // There is no position-independent addressing mode on this target.
  return RM.value_or(Reloc::Static);
}

HCS08TargetMachine::HCS08TargetMachine(
    const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
    const TargetOptions &Options, std::optional<Reloc::Model> RM,
    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT.computeDataLayout(), TT, CPU, FS, Options,
                               getEffectiveRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()) {
  initAsmInfo();
}

HCS08TargetMachine::~HCS08TargetMachine() = default;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeHCS08Target() {
  RegisterTargetMachine<HCS08TargetMachine> X(getTheHCS08Target());
}
