//===-- HCS08FrameLowering.cpp - HCS08 Frame Lowering --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HCS08FrameLowering.h"
#include "HCS08Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"

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
  // Phase 0: only frameless functions are supported. Real prologue emission
  // (ais #-frameSize, etc.) lands in Phase 1.
  assert(MF.getFrameInfo().getStackSize() == 0 &&
         "HCS08 prologue emission not yet implemented");
}

void HCS08FrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {}

MachineBasicBlock::iterator HCS08FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  return MBB.erase(I);
}
