//===-- HCS08FuseCompareBranch.cpp - Keep a branch with its compare -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Merges a flag-setting instruction and the conditional branch that reads it
// into a single CMPBR pseudo, which expandPostRAPseudo splits apart again once
// register allocation is over.
//
// Adjacency is not enough. Every reload on this machine is an lda, and lda
// sets N and Z; register allocation inserts reloads without consulting the
// liveness of a reserved register, because LLVM assumes throughout that spill
// code does not clobber flags. A loop whose carried value is live out of the
// latch gets that reload placed at the end of the block - between the compare
// and the branch - and the branch then tests the reloaded value:
//
//     lda  $03,sp
//     cmp  #$01
//     lda  $07,sp     <- reload
//     bne  .LBB0_10   <- tests the reload
//
// Neither instruction can move: the compare needs the accumulator to hold the
// value being compared and the reload needs it to hold something else. The
// only fix is for there to be no gap to insert into.
//
//===----------------------------------------------------------------------===//

#include "HCS08.h"
#include "HCS08InstrInfo.h"
#include "HCS08Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

#define DEBUG_TYPE "hcs08-fuse-compare-branch"
#define PASS_NAME "HCS08 compare/branch fusion"

namespace {

class HCS08FuseCompareBranch : public MachineFunctionPass {
public:
  static char ID;
  HCS08FuseCompareBranch() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return PASS_NAME; }
};

} // namespace

char HCS08FuseCompareBranch::ID = 0;

INITIALIZE_PASS(HCS08FuseCompareBranch, DEBUG_TYPE, PASS_NAME, false, false)

/// Can this instruction be folded into the branch that follows it?
///
/// It has to set the flags and produce nothing else: a compare, a test, or the
/// decrement a countdown loop ends with. Anything that also defines a register
/// would have to keep doing so from inside the pseudo, and nothing generates
/// that shape - a conditional branch here is always preceded by an explicit
/// comparison.
static bool isFoldableFlagSetter(const MachineInstr &MI) {
  return MI.getNumExplicitDefs() == 0 && !MI.isTerminator() &&
         MI.definesRegister(HCS08::NZV, /*TRI=*/nullptr);
}

bool HCS08FuseCompareBranch::runOnMachineFunction(MachineFunction &MF) {
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
      if (!HCS08InstrInfo::isCondBranchOpcode(MI.getOpcode()))
        continue;
      if (MI.getIterator() == MBB.begin())
        continue;

      MachineInstr &Flags = *std::prev(MI.getIterator());
      if (!isFoldableFlagSetter(Flags))
        continue;

      auto Fused = BuildMI(MBB, Flags, MI.getDebugLoc(), TII.get(HCS08::CMPBR))
                       .addImm(MI.getOpcode())
                       .addImm(Flags.getOpcode())
                       .addMBB(MI.getOperand(0).getMBB());
      for (const MachineOperand &MO : Flags.explicit_operands())
        Fused.add(MO);
      Fused.cloneMemRefs(Flags);

      Flags.eraseFromParent();
      MI.eraseFromParent();
      Changed = true;
    }
  }

  return Changed;
}

FunctionPass *llvm::createHCS08FuseCompareBranchPass() {
  return new HCS08FuseCompareBranch();
}
