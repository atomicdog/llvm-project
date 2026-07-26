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

/// Move SP by Amount, which ais can only do a signed byte at a time.
///
/// A frame bigger than that takes more than one, which is two bytes each and
/// still cheaper than the tsx/aix/txs round trip - aix takes a signed byte too,
/// so that would need just as many and clobber H:X besides. The asymmetry is
/// real and worth spelling out: allocating can step -128 but freeing can only
/// step +127, so the two directions do not always use the same count.
static void adjustSP(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                     const DebugLoc &DL, const TargetInstrInfo &TII,
                     int64_t Amount) {
  while (Amount != 0) {
    int64_t Step = Amount < 0 ? std::max<int64_t>(Amount, -128)
                              : std::min<int64_t>(Amount, 127);
    BuildMI(MBB, MBBI, DL, TII.get(HCS08::AISi)).addImm(Step);
    Amount -= Step;
  }
}

void HCS08FrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  int64_t StackSize = MF.getFrameInfo().getStackSize();
  if (StackSize == 0)
    return;

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  adjustSP(MBB, MBBI, DL, TII, -StackSize);
}

void HCS08FrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  int64_t StackSize = MF.getFrameInfo().getStackSize();
  if (StackSize == 0)
    return;

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  adjustSP(MBB, MBBI, DL, TII, StackSize);
}

MachineBasicBlock::iterator HCS08FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  // The call frame is reserved, so the markers just disappear.
  return MBB.erase(I);
}

bool HCS08FrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return true;
}
