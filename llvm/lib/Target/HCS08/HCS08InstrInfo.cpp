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

unsigned HCS08InstrInfo::getCondBranchOpcode(unsigned CC) {
  // Indexed by HCS08CC::CondCode.
  static const unsigned BccOpc[] = {
      HCS08::BEQcc, HCS08::BNEcc, HCS08::BHScc, HCS08::BLOcc,
      HCS08::BHIcc, HCS08::BLScc, HCS08::BGEcc, HCS08::BLTcc,
      HCS08::BGTcc, HCS08::BLEcc, HCS08::BMIcc, HCS08::BPLcc};
  assert(CC < std::size(BccOpc) && "invalid HCS08 condition");
  return BccOpc[CC];
}

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

bool HCS08InstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  // The low byte's operation sets the carry and the high byte's consumes it.
  unsigned LoOpc, HiOpc;
  switch (MI.getOpcode()) {
  case HCS08::ZEXT8to16:
  case HCS08::SEXT8to16: {
    // The value is in A and wanted in H:X. tax moves it to the low half; what
    // differs is what H gets. For the sign extension, lsla puts the sign bit
    // in the carry and 0 - 0 - carry is 0x00 or 0xFF, which psha/pulh carries
    // into H - clra leaves the carry alone, which is what makes that work.
    MachineBasicBlock &MBB = *MI.getParent();
    DebugLoc DL = MI.getDebugLoc();

    BuildMI(MBB, MI, DL, get(HCS08::TAX));
    if (MI.getOpcode() == HCS08::ZEXT8to16) {
      BuildMI(MBB, MI, DL, get(HCS08::CLRH));
    } else {
      BuildMI(MBB, MI, DL, get(HCS08::LSLA));
      BuildMI(MBB, MI, DL, get(HCS08::CLRA));
      BuildMI(MBB, MI, DL, get(HCS08::SBC_imm)).addImm(0);
      BuildMI(MBB, MI, DL, get(HCS08::PSHA));
      BuildMI(MBB, MI, DL, get(HCS08::PULH));
    }
    MI.eraseFromParent();
    return true;
  }
  case HCS08::FRAMEADDR: {
    // The frame index is an SP-relative displacement by now, and n,sp means
    // SP+n, so the address wanted is SP+Disp. tsx gets to SP+1; aix walks the
    // rest, a signed byte at a time.
    MachineBasicBlock &MBB = *MI.getParent();
    DebugLoc DL = MI.getDebugLoc();
    Register Dst = MI.getOperand(0).getReg();
    int64_t Off = MI.getOperand(2).getImm() - 1;
    assert(Off >= 0 && "frame object below the top of the stack");

    BuildMI(MBB, MI, DL, get(HCS08::TSXd), Dst);
    while (Off > 0) {
      int64_t Step = std::min<int64_t>(Off, 127);
      BuildMI(MBB, MI, DL, get(HCS08::AIXi), Dst).addReg(Dst).addImm(Step);
      Off -= Step;
    }
    MI.eraseFromParent();
    return true;
  }
  case HCS08::ADD16m: LoOpc = HCS08::ADD8sp; HiOpc = HCS08::ADC8sp; break;
  case HCS08::SUB16m: LoOpc = HCS08::SUB8sp; HiOpc = HCS08::SBC8sp; break;
  // The logical operations have no carry to propagate; both halves are the
  // same instruction.
  case HCS08::AND16m: LoOpc = HiOpc = HCS08::AND8sp; break;
  case HCS08::ORA16m: LoOpc = HiOpc = HCS08::ORA8sp; break;
  case HCS08::EOR16m: LoOpc = HiOpc = HCS08::EOR8sp; break;
  default:
    return false;
  }

  // Both operands are frame slots that prologue/epilogue insertion has already
  // resolved to SP-relative displacements. This runs after everything that
  // could have inserted an instruction into the middle of the chain, which is
  // the point: the carry has to survive from the low byte to the high one.
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  Register Dst = MI.getOperand(0).getReg();
  Register BaseA = MI.getOperand(1).getReg();
  int64_t DispA = MI.getOperand(2).getImm();
  Register BaseB = MI.getOperand(3).getReg();
  int64_t DispB = MI.getOperand(4).getImm();

  assert(isUInt<8>(DispA + 1) && isUInt<8>(DispB + 1) &&
         "HCS08 scratch word out of SP-relative range");

  // The result is built in the first slot, low byte first, then loaded into
  // H:X. Big-endian, so the low byte is the higher address.
  for (int Byte = 1; Byte >= 0; --Byte) {
    BuildMI(MBB, MI, DL, get(HCS08::LDAsp), HCS08::A)
        .addReg(BaseA)
        .addImm(DispA + Byte);
    BuildMI(MBB, MI, DL, get(Byte == 1 ? LoOpc : HiOpc), HCS08::A)
        .addReg(HCS08::A)
        .addReg(BaseB)
        .addImm(DispB + Byte);
    BuildMI(MBB, MI, DL, get(HCS08::STAsp))
        .addReg(HCS08::A)
        .addReg(BaseA)
        .addImm(DispA + Byte);
  }
  BuildMI(MBB, MI, DL, get(HCS08::LDHXsp), Dst).addReg(BaseA).addImm(DispA);

  MI.eraseFromParent();
  return true;
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
