//===-- HCS08InstrInfo.cpp - HCS08 Instruction Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HCS08InstrInfo.h"
#include "HCS08.h"
#include "HCS08Subtarget.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "HCS08GenInstrInfo.inc"

void HCS08InstrInfo::anchor() {}

HCS08InstrInfo::HCS08InstrInfo(const HCS08Subtarget &STI)
    : HCS08GenInstrInfo(STI, RI, HCS08::ADJCALLSTACKDOWN,
                        HCS08::ADJCALLSTACKUP),
      RI() {}

void HCS08InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL, Register DestReg,
                                 Register SrcReg, bool KillSrc,
                                 bool RenamableDest, bool RenamableSrc) const {
  // Phase 0 lowers only trivial functions and never copies between physical
  // registers. Real copies (TAX/TXA, pshx/pulx, ...) land in Phase 1.
  report_fatal_error("HCS08 copyPhysReg not yet implemented");
}

void HCS08InstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MI,
                                         Register SrcReg, bool isKill,
                                         int FrameIndex,
                                         const TargetRegisterClass *RC,
                                         Register VReg,
                                         MachineInstr::MIFlag Flags) const {
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  unsigned Opc;
  if (HCS08::GR8RegClass.hasSubClassEq(RC))
    Opc = HCS08::STAsp;
  else if (HCS08::GR16RegClass.hasSubClassEq(RC))
    Opc = HCS08::STHXsp;
  else
    report_fatal_error("cannot store this register class to a stack slot");

  BuildMI(MBB, MI, DL, get(Opc))
      .addReg(SrcReg, getKillRegState(isKill))
      .addFrameIndex(FrameIndex)
      .addImm(0);
}

void HCS08InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator MI,
                                          Register DestReg, int FrameIndex,
                                          const TargetRegisterClass *RC,
                                          Register VReg, unsigned SubReg,
                                          MachineInstr::MIFlag Flags) const {
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  unsigned Opc;
  if (HCS08::GR8RegClass.hasSubClassEq(RC))
    Opc = HCS08::LDAsp;
  else if (HCS08::GR16RegClass.hasSubClassEq(RC))
    Opc = HCS08::LDHXsp;
  else
    report_fatal_error("cannot load this register class from a stack slot");

  BuildMI(MBB, MI, DL, get(Opc), DestReg).addFrameIndex(FrameIndex).addImm(0);
}

MachineInstr *HCS08InstrInfo::foldMemoryOperandImpl(
    MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
    int FrameIndex, MachineInstr *&CopyMI, LiveIntervals *LIS,
    VirtRegMap *VRM) const {
  // Fold the second source of a reg-reg ALU pseudo (operand 2) into the
  // stack-relative form that reads it from the spill slot. The tied first
  // source stays in A.
  if (Ops.size() != 1 || Ops[0] != 2)
    return nullptr;

  unsigned SpOpc;
  switch (MI.getOpcode()) {
  case HCS08::ADD8rr: SpOpc = HCS08::ADD8sp; break;
  case HCS08::SUB8rr: SpOpc = HCS08::SUB8sp; break;
  case HCS08::AND8rr: SpOpc = HCS08::AND8sp; break;
  case HCS08::ORA8rr: SpOpc = HCS08::ORA8sp; break;
  case HCS08::EOR8rr: SpOpc = HCS08::EOR8sp; break;
  default:
    return nullptr;
  }

  MachineBasicBlock::iterator InsertPt = MI;
  return BuildMI(*MI.getParent(), InsertPt, MI.getDebugLoc(), get(SpOpc))
      .add(MI.getOperand(0)) // dst
      .add(MI.getOperand(1)) // tied src (A)
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .getInstr();
}

static bool isCondBranchOpc(unsigned Opc) {
  switch (Opc) {
  case HCS08::BEQcc:
  case HCS08::BNEcc:
  case HCS08::BHScc:
  case HCS08::BLOcc:
  case HCS08::BHIcc:
  case HCS08::BLScc:
  case HCS08::BGEcc:
  case HCS08::BLTcc:
  case HCS08::BGTcc:
  case HCS08::BLEcc:
  case HCS08::BMIcc:
  case HCS08::BPLcc:
    return true;
  default:
    return false;
  }
}

static unsigned getOppositeBranchOpc(unsigned Opc) {
  switch (Opc) {
  case HCS08::BEQcc: return HCS08::BNEcc;
  case HCS08::BNEcc: return HCS08::BEQcc;
  case HCS08::BHScc: return HCS08::BLOcc;
  case HCS08::BLOcc: return HCS08::BHScc;
  case HCS08::BHIcc: return HCS08::BLScc;
  case HCS08::BLScc: return HCS08::BHIcc;
  case HCS08::BGEcc: return HCS08::BLTcc;
  case HCS08::BLTcc: return HCS08::BGEcc;
  case HCS08::BGTcc: return HCS08::BLEcc;
  case HCS08::BLEcc: return HCS08::BGTcc;
  case HCS08::BMIcc: return HCS08::BPLcc;
  case HCS08::BPLcc: return HCS08::BMIcc;
  default:
    llvm_unreachable("not an HCS08 conditional branch");
  }
}

bool HCS08InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *&TBB,
                                   MachineBasicBlock *&FBB,
                                   SmallVectorImpl<MachineOperand> &Cond,
                                   bool AllowModify) const {
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end() || !isUnpredicatedTerminator(*I))
    return false;

  MachineInstr *LastInst = &*I;
  unsigned LastOpc = LastInst->getOpcode();

  // A single terminator.
  if (I == MBB.begin() || !isUnpredicatedTerminator(*std::prev(I))) {
    if (LastOpc == HCS08::BRAcc) {
      TBB = LastInst->getOperand(0).getMBB();
      return false;
    }
    if (isCondBranchOpc(LastOpc)) {
      TBB = LastInst->getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(LastOpc));
      return false;
    }
    return true; // Unknown terminator.
  }

  MachineInstr *SecondLastInst = &*std::prev(I);
  unsigned SecondLastOpc = SecondLastInst->getOpcode();

  // Conditional branch followed by an unconditional branch.
  if (isCondBranchOpc(SecondLastOpc) && LastOpc == HCS08::BRAcc) {
    TBB = SecondLastInst->getOperand(0).getMBB();
    Cond.push_back(MachineOperand::CreateImm(SecondLastOpc));
    FBB = LastInst->getOperand(0).getMBB();
    return false;
  }

  // Two unconditional branches: the second is dead.
  if (SecondLastOpc == HCS08::BRAcc && LastOpc == HCS08::BRAcc) {
    if (!AllowModify)
      return true;
    TBB = SecondLastInst->getOperand(0).getMBB();
    I->eraseFromParent();
    return false;
  }

  return true;
}

unsigned HCS08InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                      int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  unsigned Count = 0;
  while (I != MBB.begin()) {
    if (I->getOpcode() != HCS08::BRAcc && !isCondBranchOpc(I->getOpcode()))
      break;
    MachineBasicBlock::iterator ToDelete = I;
    --I;
    ToDelete->eraseFromParent();
    ++Count;
  }
  return Count;
}

unsigned HCS08InstrInfo::insertBranch(MachineBasicBlock &MBB,
                                      MachineBasicBlock *TBB,
                                      MachineBasicBlock *FBB,
                                      ArrayRef<MachineOperand> Cond,
                                      const DebugLoc &DL,
                                      int *BytesAdded) const {
  assert(!BytesAdded && "code size not handled");
  if (Cond.empty()) {
    assert(!FBB && "unconditional branch with two targets");
    BuildMI(&MBB, DL, get(HCS08::BRAcc)).addMBB(TBB);
    return 1;
  }

  BuildMI(&MBB, DL, get(Cond[0].getImm())).addMBB(TBB);
  if (!FBB)
    return 1;
  BuildMI(&MBB, DL, get(HCS08::BRAcc)).addMBB(FBB);
  return 2;
}

bool HCS08InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 1 && "expected one condition operand");
  Cond[0].setImm(getOppositeBranchOpc(Cond[0].getImm()));
  return false;
}
