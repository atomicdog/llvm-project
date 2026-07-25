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
  // Phase 0 handles only functions without stack frame indices. Frame
  // lowering (Phase 1) will implement SP-relative resolution here.
  report_fatal_error("HCS08 frame index elimination not yet implemented");
}

Register HCS08RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return HCS08::SP;
}
