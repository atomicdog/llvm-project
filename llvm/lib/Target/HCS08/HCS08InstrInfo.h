//===-- HCS08InstrInfo.h - HCS08 Instruction Information --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HCS08_HCS08INSTRINFO_H
#define LLVM_LIB_TARGET_HCS08_HCS08INSTRINFO_H

#include "HCS08RegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "HCS08GenInstrInfo.inc"

namespace llvm {

class HCS08Subtarget;

class HCS08InstrInfo : public HCS08GenInstrInfo {
  const HCS08RegisterInfo RI;
  virtual void anchor();

public:
  explicit HCS08InstrInfo(const HCS08Subtarget &STI);

  const HCS08RegisterInfo &getRegisterInfo() const { return RI; }

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, Register DestReg, Register SrcReg,
                   bool KillSrc, bool RenamableDest = false,
                   bool RenamableSrc = false) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HCS08_HCS08INSTRINFO_H
