//===-- HCS08FrameLowering.cpp - HCS08 Frame Lowering --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HCS08FrameLowering.h"
#include "HCS08.h"
#include "HCS08InstrInfo.h"
#include "HCS08Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

HCS08FrameLowering::HCS08FrameLowering(const HCS08Subtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown,
                          /*StackAlignment=*/Align(1),
                          /*LocalAreaOffset=*/0) {}

bool HCS08FrameLowering::hasFPImpl(const MachineFunction &MF) const {
  return false;
}

void HCS08FrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  int64_t StackSize = MF.getFrameInfo().getStackSize();
  if (StackSize == 0)
    return;

  // ais takes a signed byte; larger frames need a different sequence.
  assert(StackSize <= 128 && "HCS08 frame too large for a single ais");

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  BuildMI(MBB, MBBI, DL, TII.get(HCS08::AISi)).addImm(-StackSize);
}

void HCS08FrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  int64_t StackSize = MF.getFrameInfo().getStackSize();
  if (StackSize == 0)
    return;

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  BuildMI(MBB, MBBI, DL, TII.get(HCS08::AISi)).addImm(StackSize);
}

MachineBasicBlock::iterator HCS08FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  return MBB.erase(I);
}
