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
// Which means the adjacency this pass folds has to be established rather than
// assumed. Earlier passes are entitled to put something between a compare and
// its branch as long as they believe it leaves the flags alone - MachineCSE
// hoists a common subexpression into the dominating block, and it lands before
// the terminator, which is after the compare. Folding only what is already
// adjacent silently skips those, and a skipped pair is the one allocation then
// breaks. So a gap whose contents are indifferent to the flags is closed here
// first, by sinking the compare to meet its branch.
//
//===----------------------------------------------------------------------===//

#include "HCS08.h"
#include "HCS08InstrInfo.h"
#include "HCS08Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

// A way to take this pass out of the pipeline, matching the one
// HCS08StackToIndexed already has. Both exist for the same reason: these are
// target-specific passes with no equivalent anywhere else, so when a program
// miscompiles they are the first things worth eliminating, and doing that by
// editing and rebuilding is slow enough to discourage it.
static cl::opt<bool>
    DisablePass("hcs08-no-fuse-compare-branch", cl::Hidden,
                cl::desc("Leave HCS08 compares and branches unfused"));

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

/// Can the flag setter move down across \p MI to reach its branch?
///
/// Being next to the branch is not something the flag setter is given; a pass
/// that believes it is leaving the flags alone may put an instruction in
/// between, and MachineCSE does exactly that - it sinks a common subexpression
/// to the end of the block it dominates, which is after the compare and before
/// the terminator. So the gap has to be closed rather than given up on, and
/// what makes that safe is that the instruction sitting in it does not care
/// when the flags are set.
static bool canSinkFlagSetterAcross(const MachineInstr &Flags,
                                    const MachineInstr &MI,
                                    const TargetRegisterInfo &TRI) {
  if (MI.isTerminator() || MI.isCall() || MI.isInlineAsm() ||
      MI.isPosition() || MI.isBundle() || MI.hasUnmodeledSideEffects())
    return false;

  // Anything that reads either flag group would see the wrong one afterwards,
  // and anything that writes one is itself what the branch reads - in which
  // case this compare is dead and moving it would be a lie.
  for (MCRegister Flag : {MCRegister(HCS08::NZV), MCRegister(HCS08::C)})
    if (MI.readsRegister(Flag, &TRI) || MI.modifiesRegister(Flag, &TRI))
      return false;

  // The compare has to keep reading what it read before, so nothing it reads
  // may be written by the instruction it moves past.
  for (const MachineOperand &MO : MI.operands())
    if (MO.isReg() && MO.isDef() && MO.getReg() &&
        Flags.readsRegister(MO.getReg(), &TRI))
      return false;

  // The foldable set includes the read-modify-write decrement a countdown loop
  // ends with, so the flag setter is not always load-only. Two loads may swap;
  // a store may not cross anything that touches memory.
  if ((Flags.mayStore() && MI.mayLoadOrStore()) ||
      (MI.mayStore() && Flags.mayLoadOrStore()))
    return false;

  return true;
}

bool HCS08FuseCompareBranch::runOnMachineFunction(MachineFunction &MF) {
  if (DisablePass)
    return false;

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
      if (!HCS08InstrInfo::isCondBranchOpcode(MI.getOpcode()))
        continue;
      if (MI.getIterator() == MBB.begin())
        continue;

      // Whatever last wrote NZV is what this branch reads, however far back it
      // is. Looking only at the immediately preceding instruction would miss a
      // compare that something else has been slipped in front of, and missing
      // it is not neutral: the pair stays unfused, and allocation then fills
      // the same gap with a reload, which is an lda, which sets N and Z.
      MachineInstr *Flags = nullptr;
      SmallVector<MachineInstr *, 4> Between;
      for (MachineBasicBlock::iterator It = MI.getIterator();
           It != MBB.begin() && !Flags;) {
        MachineInstr &Prev = *--It;
        if (Prev.isDebugInstr())
          continue;
        if (Prev.modifiesRegister(HCS08::NZV, &TRI))
          Flags = &Prev;
        else
          Between.push_back(&Prev);
      }

      if (!Flags || !isFoldableFlagSetter(*Flags))
        continue;
      if (!llvm::all_of(Between, [&](const MachineInstr *Gap) {
            return canSinkFlagSetterAcross(*Flags, *Gap, TRI);
          }))
        continue;

      // Close the gap before folding, so that what is folded is adjacent.
      if (!Between.empty())
        MBB.splice(MI.getIterator(), &MBB, Flags->getIterator());

      auto Fused = BuildMI(MBB, *Flags, MI.getDebugLoc(), TII.get(HCS08::CMPBR))
                       .addImm(MI.getOpcode())
                       .addImm(Flags->getOpcode())
                       .addMBB(MI.getOperand(0).getMBB());
      for (const MachineOperand &MO : Flags->explicit_operands())
        Fused.add(MO);
      Fused.cloneMemRefs(*Flags);

      Flags->eraseFromParent();
      MI.eraseFromParent();
      Changed = true;
    }
  }

  return Changed;
}

FunctionPass *llvm::createHCS08FuseCompareBranchPass() {
  return new HCS08FuseCompareBranch();
}
