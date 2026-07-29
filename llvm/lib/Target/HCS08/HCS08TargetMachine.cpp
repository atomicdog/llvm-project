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

  // Both the prologue and the epilogue describe themselves to .debug_frame, so
  // a block laid out after an epilogue but reached with the frame still up
  // would inherit the epilogue's rules and be wrong for its whole length. That
  // is any function with an early return, not a corner case. The pass inserts
  // the .cfi_remember_state/.cfi_restore_state pair that fixes it, and only
  // runs when something asked for frame moves in the first place.
  setCFIFixup(true);
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
  void addPreRegAlloc() override;
  void addPreEmitPass() override;
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

void HCS08PassConfig::addPreEmitPass() {
  // Ahead of the pass below, which would otherwise spend a tsx on accesses
  // that are about to leave the frame entirely. Like it, this wants
  // expandPostRAPseudo to have run: a spill slot is only recognisable as one
  // load and one store once the 16-bit pseudos it feeds have come apart.
  addPass(createHCS08DirectPageBankPass());
  // Shortens instructions, so it has to run before anything measures them -
  // and after expandPostRAPseudo, which is what produces the 16-bit ALU chains
  // that are the best runs it finds.
  addPass(createHCS08StackToIndexedPass());
  // Relative branches reach a signed byte, which real code does not stay
  // inside; out-of-range ones become jmp.
  addPass(&BranchRelaxationPassID);
}

void HCS08PassConfig::addPreRegAlloc() {
  // Has to run before allocation, which is what would otherwise put a reload
  // between a compare and its branch.
  addPass(createHCS08FuseCompareBranchPass());
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeHCS08Target() {
  RegisterTargetMachine<HCS08TargetMachine> X(getTheHCS08Target());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeHCS08DAGToDAGISelLegacyPass(PR);
  initializeHCS08AsmPrinterPass(PR);
}
