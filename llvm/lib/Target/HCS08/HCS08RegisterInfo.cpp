//===-- HCS08RegisterInfo.cpp - HCS08 Register Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HCS08RegisterInfo.h"
#include "HCS08.h"
#include "HCS08Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "HCS08GenRegisterInfo.inc"

// The return-address slot is not a register on HCS08; pass PC as the RA marker.
HCS08RegisterInfo::HCS08RegisterInfo() : HCS08GenRegisterInfo(HCS08::PC) {}

const MCPhysReg *
HCS08RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_HCS08_SaveList;
}

BitVector HCS08RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  Reserved.set(HCS08::SP);
  Reserved.set(HCS08::PC);
  Reserved.set(HCS08::NZV);
  Reserved.set(HCS08::C);
  // Not a register, only a marker distinguishing a bank offset from a frame
  // displacement in a pseudo's base operand.
  Reserved.set(HCS08::DPB);
  return Reserved;
}

const TargetRegisterClass *
HCS08RegisterInfo::getPointerRegClass(unsigned Kind) const {
  return &HCS08::GR16RegClass;
}

bool HCS08RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                            int SPAdj, unsigned FIOperandNum,
                                            RegScavenger *RS) const {
  assert(SPAdj == 0 && "unexpected SP adjustment");
  MachineInstr &MI = *II;
  const MachineFunction &MF = *MI.getParent()->getParent();
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  int FI = MI.getOperand(FIOperandNum).getIndex();

  // The n,sp addressing mode computes SP+n, and SP points one byte *below*
  // the last thing pushed, so the lowest byte of the frame is 1,sp and 0,sp
  // is the byte the next push will take. (Verified against an independent HC08
  // model; getting this wrong puts every object one byte low, and the bottom
  // one is then overwritten by the return address of the next call.)
  //
  // Frame-object offsets are measured from the entry SP: locals are negative
  // (below it), incoming stack arguments positive (above it, past the return
  // address). Object N lives at entry-SP + 1 + N, so rebasing onto the
  // post-prologue SP gives N + StackSize + 1.
  int64_t Offset = MFI.getObjectOffset(FI) + (int64_t)MFI.getStackSize() + 1;
  Offset += MI.getOperand(FIOperandNum + 1).getImm();

  // The displacement of every n,sp form is one unsigned byte, so a frame
  // object more than 255 bytes above SP cannot be reached by one at all. It
  // would need the $nnnn,sp forms - which lda and sta have and ldhx, sthx and
  // cphx do not - or the address computed into H:X, and neither is implemented.
  //
  // This was an assert, which is to say that a release build wrote the low byte
  // of the offset and carried on: an access to a different frame object, chosen
  // by arithmetic, with nothing said. Refusing is worse code than the fix and
  // better than that.
  if (Offset < 0 || !isUInt<8>(Offset))
    report_fatal_error(
        "HCS08 frame object out of reach at SP+" + Twine(Offset) + " in '" +
            MF.getName() +
            "': the n,sp displacement is one byte, so the frame and any "
            "incoming stack arguments have to fit in 256 bytes together",
        /*GenCrashDiag=*/false);

  MI.getOperand(FIOperandNum).ChangeToRegister(HCS08::SP, /*isDef=*/false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
  return false;
}

Register HCS08RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return HCS08::SP;
}
