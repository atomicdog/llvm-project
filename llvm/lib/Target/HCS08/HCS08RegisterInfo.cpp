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
  Reserved.set(HCS08::CCR);
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

  // Object offsets are negative (below the frame base); adding the frame size
  // maps them into [0, StackSize). The n,sp addressing computes SP+1+n, which
  // is where the allocated bytes live after "ais #-StackSize".
  int64_t Offset = MFI.getObjectOffset(FI) + (int64_t)MFI.getStackSize();
  Offset += MI.getOperand(FIOperandNum + 1).getImm();

  assert(Offset >= 0 && isUInt<8>(Offset) &&
         "HCS08 SP-relative offset out of range (needs SP2/H:X base)");

  MI.getOperand(FIOperandNum).ChangeToRegister(HCS08::SP, /*isDef=*/false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
  return false;
}

Register HCS08RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return HCS08::SP;
}
