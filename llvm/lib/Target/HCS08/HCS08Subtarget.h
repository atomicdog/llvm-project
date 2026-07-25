//===-- HCS08Subtarget.h - Define Subtarget for the HCS08 ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HCS08_HCS08SUBTARGET_H
#define LLVM_LIB_TARGET_HCS08_HCS08SUBTARGET_H

#include "HCS08FrameLowering.h"
#include "HCS08ISelLowering.h"
#include "HCS08InstrInfo.h"
#include "HCS08RegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include <memory>
#include <string>

#define GET_SUBTARGETINFO_HEADER
#include "HCS08GenSubtargetInfo.inc"

namespace llvm {
class StringRef;
class TargetMachine;

class HCS08Subtarget : public HCS08GenSubtargetInfo {
  virtual void anchor();

  bool HasHCS08 = false;

  HCS08InstrInfo InstrInfo;
  HCS08TargetLowering TLInfo;
  HCS08FrameLowering FrameLowering;
  std::unique_ptr<const SelectionDAGTargetInfo> TSInfo;

public:
  HCS08Subtarget(const Triple &TT, const std::string &CPU,
                 const std::string &FS, const TargetMachine &TM);
  ~HCS08Subtarget() override;

  HCS08Subtarget &initializeSubtargetDependencies(StringRef CPU, StringRef FS);

  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  const HCS08InstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const TargetFrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const HCS08RegisterInfo *getRegisterInfo() const override {
    return &InstrInfo.getRegisterInfo();
  }
  const HCS08TargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HCS08_HCS08SUBTARGET_H
