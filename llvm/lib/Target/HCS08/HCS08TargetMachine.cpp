//===-- HCS08TargetMachine.cpp - Define TargetMachine for HCS08 ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HCS08TargetMachine.h"
#include "HCS08.h"
#include "HCS08MachineFunctionInfo.h"
#include "TargetInfo/HCS08TargetInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
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
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  initAsmInfo();
}

HCS08TargetMachine::~HCS08TargetMachine() = default;

namespace {
/// HCS08 Code Generator Pass Configuration Options.
class HCS08PassConfig : public TargetPassConfig {
public:
  HCS08PassConfig(HCS08TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  HCS08TargetMachine &getHCS08TargetMachine() const {
    return getTM<HCS08TargetMachine>();
  }

  bool addInstSelector() override;
};
} // namespace

TargetPassConfig *HCS08TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new HCS08PassConfig(*this, PM);
}

MachineFunctionInfo *HCS08TargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return HCS08MachineFunctionInfo::create<HCS08MachineFunctionInfo>(Allocator,
                                                                    F, STI);
}

bool HCS08PassConfig::addInstSelector() {
  addPass(createHCS08ISelDag(getHCS08TargetMachine(), getOptLevel()));
  return false;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeHCS08Target() {
  RegisterTargetMachine<HCS08TargetMachine> X(getTheHCS08Target());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeHCS08DAGToDAGISelLegacyPass(PR);
  initializeHCS08AsmPrinterPass(PR);
}
