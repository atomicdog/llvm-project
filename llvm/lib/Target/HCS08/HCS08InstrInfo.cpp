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
#include "llvm/CodeGen/MachineFrameInfo.h"
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
  // A copy to the register it came from is erased before it reaches here, and
  // H:X is the only 16-bit register, so every copy left is between two of the
  // three bytes A, H and X.
  if (DestReg == SrcReg)
    return;

  // These are the assembler's implicit-operand forms - "tax" names neither of
  // the registers it touches - so what each one reads and writes has to be
  // spelled out on the MachineInstr.
  auto Transfer = [&](unsigned Opc) {
    BuildMI(MBB, I, DL, get(Opc))
        .addReg(DestReg, RegState::Define | RegState::Implicit)
        .addReg(SrcReg, RegState::Implicit | getKillRegState(KillSrc));
  };

  // Nothing transfers to or from H, so those copies go over the stack. The
  // pair has to stay adjacent: SP is one low in between, and an "n,sp" operand
  // landing there would read the wrong byte.
  auto ViaStack = [&](unsigned PushOpc, unsigned PullOpc) {
    BuildMI(MBB, I, DL, get(PushOpc))
        .addReg(SrcReg, RegState::Implicit | getKillRegState(KillSrc));
    BuildMI(MBB, I, DL, get(PullOpc))
        .addReg(DestReg, RegState::Define | RegState::Implicit);
  };

  // None of these six touch the condition codes, which is what lets the
  // allocator insert a copy between a compare and the branch that reads it.
  if (DestReg == HCS08::X && SrcReg == HCS08::A)
    return Transfer(HCS08::TAX);
  if (DestReg == HCS08::A && SrcReg == HCS08::X)
    return Transfer(HCS08::TXA);
  if (DestReg == HCS08::H && SrcReg == HCS08::A)
    return ViaStack(HCS08::PSHA, HCS08::PULH);
  if (DestReg == HCS08::H && SrcReg == HCS08::X)
    return ViaStack(HCS08::PSHX, HCS08::PULH);
  if (DestReg == HCS08::A && SrcReg == HCS08::H)
    return ViaStack(HCS08::PSHH, HCS08::PULA);
  if (DestReg == HCS08::X && SrcReg == HCS08::H)
    return ViaStack(HCS08::PSHH, HCS08::PULX);

  // SP is deliberately not reachable this way. tsx and txs look like the
  // missing transfers but are off by one - tsx gives SP+1 and txs takes H:X-1 -
  // so a copy lowered to either of them would be wrong by a byte.
  report_fatal_error("cannot copy between these HCS08 registers");
}

/// The whole-slot frame access these opcodes perform, or nothing.
///
/// Both halves of the contract matter. The displacement has to be zero and the
/// width has to be the whole object, because the 16-bit chains reach into a
/// slot a byte at a time and reporting one of those as "the value is in this
/// slot" would have the spiller delete an access that only moves half of it.
static Register matchSlotAccess(const MachineInstr &MI, int &FrameIndex,
                                unsigned Bytes, unsigned RegOp,
                                unsigned BaseOp) {
  if (!MI.getOperand(BaseOp).isFI() || MI.getOperand(BaseOp + 1).getImm() != 0)
    return Register();
  int FI = MI.getOperand(BaseOp).getIndex();
  if (MI.getMF()->getFrameInfo().getObjectSize(FI) != Bytes)
    return Register();
  FrameIndex = FI;
  return MI.getOperand(RegOp).getReg();
}

Register HCS08InstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                             int &FrameIndex) const {
  switch (MI.getOpcode()) {
  case HCS08::LDAsp:
    return matchSlotAccess(MI, FrameIndex, 1, /*RegOp=*/0, /*BaseOp=*/1);
  case HCS08::LDHXsp:
    return matchSlotAccess(MI, FrameIndex, 2, /*RegOp=*/0, /*BaseOp=*/1);
  default:
    return Register();
  }
}

Register HCS08InstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                            int &FrameIndex) const {
  switch (MI.getOpcode()) {
  case HCS08::STAsp:
    return matchSlotAccess(MI, FrameIndex, 1, /*RegOp=*/0, /*BaseOp=*/1);
  case HCS08::STHXsp:
    return matchSlotAccess(MI, FrameIndex, 2, /*RegOp=*/0, /*BaseOp=*/1);
  default:
    return Register();
  }
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

  // Spill code writes NZV and nothing ever reads it back: a compare is welded
  // to its branch before allocation (section 14), so the flags are not live
  // across an arbitrary instruction, and the one thing that does travel - the
  // carry of a 16-bit chain - is register C, which these do not touch. Saying
  // so is what lets the spiller delete one of these when it turns out to be
  // redundant; without it allDefsAreDead is false and it cannot.
  BuildMI(MBB, MI, DL, get(Opc))
      .addReg(SrcReg, getKillRegState(isKill))
      .addFrameIndex(FrameIndex)
      .addImm(0)
      ->addRegisterDead(HCS08::NZV, &RI);
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

  BuildMI(MBB, MI, DL, get(Opc), DestReg)
      .addFrameIndex(FrameIndex)
      .addImm(0)
      ->addRegisterDead(HCS08::NZV, &RI);
}

bool HCS08InstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  // The low byte's operation sets the carry and the high byte's consumes it.
  unsigned LoOpc, HiOpc;
  switch (MI.getOpcode()) {
  case HCS08::ZEXT8to16:
  case HCS08::SEXT8to16:
  case HCS08::SIGNMASK16: {
    // Three ways of filling H:X from a narrower sign.
    //
    // For the two 8-to-16 extensions the value is in A and wanted in H:X. tax
    // moves it to the low half; what differs is what H gets. For the sign
    // extension, lsla puts the sign bit in the carry and 0 - 0 - carry is
    // 0x00 or 0xFF, which psha/pulh carries into H - clra leaves the carry
    // alone, which is the only reason that works.
    //
    // SIGNMASK16 wants that same 0x00-or-0xFF in *both* halves, taken from the
    // sign of a word already in H:X. The sign is in H, which nothing reads
    // directly, so pshh/pula fetches it into A first. The tax then has to come
    // after the mask is computed rather than before it, because here it is the
    // mask that belongs in X and not the value - which is why this does not
    // simply share the sequence above.
    MachineBasicBlock &MBB = *MI.getParent();
    DebugLoc DL = MI.getDebugLoc();
    bool IsMask16 = MI.getOpcode() == HCS08::SIGNMASK16;

    // These are the assembler's accumulator-implicit forms, so what they read
    // and write has to be spelled out here: between them tax and clrh (or
    // pulh) define both halves of H:X, which is what the pseudo promised.
    auto Def = RegState::Define | RegState::Implicit;

    if (IsMask16) {
      BuildMI(MBB, MI, DL, get(HCS08::PSHH))
          .addReg(HCS08::H, RegState::Implicit);
      BuildMI(MBB, MI, DL, get(HCS08::PULA)).addReg(HCS08::A, Def);
    } else {
      BuildMI(MBB, MI, DL, get(HCS08::TAX))
          .addReg(HCS08::X, Def)
          .addReg(HCS08::A, RegState::Implicit);
    }

    if (MI.getOpcode() == HCS08::ZEXT8to16) {
      BuildMI(MBB, MI, DL, get(HCS08::CLRH)).addReg(HCS08::H, Def);
    } else {
      // lsla shifts the sign bit into the carry...
      BuildMI(MBB, MI, DL, get(HCS08::LSLA))
          .addReg(HCS08::A, Def)
          .addReg(HCS08::NZV, Def)
          .addReg(HCS08::C, Def)
          .addReg(HCS08::A, RegState::Implicit);
      // ...and clra deliberately does not disturb it, which is the only
      // reason the carry is still there for the sbc below.
      BuildMI(MBB, MI, DL, get(HCS08::CLRA))
          .addReg(HCS08::A, Def)
          .addReg(HCS08::NZV, Def);
      BuildMI(MBB, MI, DL, get(HCS08::SBC_imm))
          .addImm(0)
          .addReg(HCS08::A, Def)
          .addReg(HCS08::NZV, Def)
          .addReg(HCS08::C, Def)
          .addReg(HCS08::A, RegState::Implicit)
          .addReg(HCS08::C, RegState::Implicit);
      // A now holds the mask. SIGNMASK16 wants it in X as well as H.
      if (IsMask16)
        BuildMI(MBB, MI, DL, get(HCS08::TAX))
            .addReg(HCS08::X, Def)
            .addReg(HCS08::A, RegState::Implicit);
      BuildMI(MBB, MI, DL, get(HCS08::PSHA))
          .addReg(HCS08::A, RegState::Implicit);
      BuildMI(MBB, MI, DL, get(HCS08::PULH)).addReg(HCS08::H, Def);
    }
    MI.eraseFromParent();
    return true;
  }
  case HCS08::CMPBR: {
    // Split back into the flag-setting instruction and its branch, now that
    // there is no allocator left to put anything between them. The operands
    // are the branch opcode, the flag-setting opcode, the destination, and
    // then whatever that instruction's own operands were.
    MachineBasicBlock &MBB = *MI.getParent();
    DebugLoc DL = MI.getDebugLoc();
    unsigned BrOpc = MI.getOperand(0).getImm();
    unsigned FlagsOpc = MI.getOperand(1).getImm();
    MachineBasicBlock *Dest = MI.getOperand(2).getMBB();

    auto Flags = BuildMI(MBB, MI, DL, get(FlagsOpc));
    for (unsigned I = 3, E = MI.getNumExplicitOperands(); I != E; ++I)
      Flags.add(MI.getOperand(I));
    Flags.cloneMemRefs(MI);
    BuildMI(MBB, MI, DL, get(BrOpc)).addMBB(Dest);

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

  // Each operand is either a frame slot, which prologue/epilogue insertion has
  // already resolved to an SP-relative displacement, or a byte offset into the
  // direct-page bank, which the base names DPB to say. This runs after
  // everything that could have inserted an instruction into the middle of the
  // chain, which is the point: the carry has to survive from the low byte to
  // the high one.
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  Register Dst = MI.getOperand(0).getReg();
  Register BaseA = MI.getOperand(1).getReg();
  int64_t DispA = MI.getOperand(2).getImm();
  Register BaseB = MI.getOperand(3).getReg();
  int64_t DispB = MI.getOperand(4).getImm();

  bool ABank = BaseA == HCS08::DPB, BBank = BaseB == HCS08::DPB;
  assert((ABank || isUInt<8>(DispA + 1)) && (BBank || isUInt<8>(DispB + 1)) &&
         "HCS08 scratch word out of SP-relative range");

  // A bank slot is two bytes to reach against the frame's three, and the
  // instruction is a different one rather than the same one shortened. This
  // yields the opcode rather than the descriptor: a MachineInstr keeps a
  // pointer to the MCInstrDesc it was built from, so handing it one that dies
  // with the enclosing expression leaves it dangling.
  auto Access = [](unsigned SPOpc, unsigned DirOpc, bool InBank) -> unsigned {
    return InBank ? DirOpc : SPOpc;
  };
  auto AddAddr = [&](const MachineInstrBuilder &MIB, Register Base,
                     int64_t Disp) {
    if (Base != HCS08::DPB) {
      MIB.addReg(Base).addImm(Disp);
      return;
    }
    MachineOperand MO = MachineOperand::CreateES(HCS08DPBankSymbol);
    MO.setOffset(Disp);
    MIB.add(MO);
  };

  unsigned LoDir = LoOpc == HCS08::ADD8sp   ? unsigned(HCS08::ADD8dir)
                   : LoOpc == HCS08::SUB8sp ? unsigned(HCS08::SUB8dir)
                   : LoOpc == HCS08::AND8sp ? unsigned(HCS08::AND8dir)
                   : LoOpc == HCS08::ORA8sp ? unsigned(HCS08::ORA8dir)
                                            : unsigned(HCS08::EOR8dir);
  unsigned HiDir = HiOpc == HCS08::ADC8sp   ? unsigned(HCS08::ADC8dir)
                   : HiOpc == HCS08::SBC8sp ? unsigned(HCS08::SBC8dir)
                                            : LoDir;

  // The result is built in the first slot, low byte first, then loaded into
  // H:X. Big-endian, so the low byte is the higher address.
  for (int Byte = 1; Byte >= 0; --Byte) {
    AddAddr(BuildMI(MBB, MI, DL,
                    get(Access(HCS08::LDAsp, HCS08::LDAdir, ABank)), HCS08::A),
            BaseA, DispA + Byte);
    AddAddr(BuildMI(MBB, MI, DL,
                    get(Access(Byte == 1 ? LoOpc : HiOpc,
                               Byte == 1 ? LoDir : HiDir, BBank)),
                    HCS08::A)
                .addReg(HCS08::A),
            BaseB, DispB + Byte);
    AddAddr(BuildMI(MBB, MI, DL,
                    get(Access(HCS08::STAsp, HCS08::STAdir, ABank)))
                .addReg(HCS08::A),
            BaseA, DispA + Byte);
  }
  AddAddr(BuildMI(MBB, MI, DL,
                  get(Access(HCS08::LDHXsp, HCS08::LDHXdir, ABank)), Dst),
          BaseA, DispA);

  MI.eraseFromParent();
  return true;
}

bool HCS08InstrInfo::isCondBranchOpcode(unsigned Opc) {
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
    if (HCS08InstrInfo::isCondBranchOpcode(LastOpc)) {
      TBB = LastInst->getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(LastOpc));
      return false;
    }
    return true; // Unknown terminator.
  }

  MachineInstr *SecondLastInst = &*std::prev(I);
  unsigned SecondLastOpc = SecondLastInst->getOpcode();

  // Conditional branch followed by an unconditional branch.
  if (HCS08InstrInfo::isCondBranchOpcode(SecondLastOpc) && LastOpc == HCS08::BRAcc) {
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
  if (BytesRemoved)
    *BytesRemoved = 0;

  // Walk back from the end rather than from the last instruction, so that a
  // block whose only instruction is a branch still has it removed. Stopping
  // one short of that leaves the caller believing it removed nothing, and
  // branch folding then has no way to make progress and does not terminate.
  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (I->getOpcode() != HCS08::BRAcc &&
        !HCS08InstrInfo::isCondBranchOpcode(I->getOpcode()))
      break;
    if (BytesRemoved)
      *BytesRemoved += getInstSizeInBytes(*I);
    I->eraseFromParent();
    I = MBB.end();
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
  if (BytesAdded)
    *BytesAdded = 0;

  auto Emit = [&](unsigned Opc, MachineBasicBlock *Dest) {
    MachineInstr &MI = *BuildMI(&MBB, DL, get(Opc)).addMBB(Dest);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
  };

  if (Cond.empty()) {
    assert(!FBB && "unconditional branch with two targets");
    Emit(HCS08::BRAcc, TBB);
    return 1;
  }

  Emit(Cond[0].getImm(), TBB);
  if (!FBB)
    return 1;
  Emit(HCS08::BRAcc, FBB);
  return 2;
}

bool HCS08InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 1 && "expected one condition operand");
  Cond[0].setImm(getOppositeBranchOpc(Cond[0].getImm()));
  return false;
}

unsigned HCS08InstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  // Every instruction on this target has a fixed size, which the format
  // classes record. The pseudos that survive to be measured carry the size of
  // what they expand into.
  return MI.getDesc().getSize();
}

bool HCS08InstrInfo::isBranchOffsetInRange(unsigned BranchOpc,
                                           int64_t BrOffset) const {
  // jmp is a 16-bit absolute address and so reaches anywhere.
  if (BranchOpc == HCS08::JMPa)
    return true;
  // Everything else is a signed byte measured from the end of the instruction.
  return isInt<8>(BrOffset);
}

MachineBasicBlock *
HCS08InstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  return MI.getOperand(0).getMBB();
}

void HCS08InstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                          MachineBasicBlock &NewDestBB,
                                          MachineBasicBlock &RestoreBB,
                                          const DebugLoc &DL, int64_t BrOffset,
                                          RegScavenger *RS) const {
  // jmp needs no register, so there is nothing to scavenge or restore.
  BuildMI(&MBB, DL, get(HCS08::JMPa)).addMBB(&NewDestBB);
}
