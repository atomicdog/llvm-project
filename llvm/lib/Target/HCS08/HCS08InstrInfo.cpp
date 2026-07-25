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
    : HCS08GenInstrInfo(STI, RI), RI() {}

void HCS08InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL, Register DestReg,
                                 Register SrcReg, bool KillSrc,
                                 bool RenamableDest, bool RenamableSrc) const {
  // Phase 0 lowers only trivial functions and never copies between physical
  // registers. Real copies (TAX/TXA, pshx/pulx, ...) land in Phase 1.
  report_fatal_error("HCS08 copyPhysReg not yet implemented");
}
