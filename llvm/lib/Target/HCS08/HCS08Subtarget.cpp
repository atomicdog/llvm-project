//===-- HCS08Subtarget.cpp - HCS08 Subtarget Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HCS08Subtarget.h"
#include "HCS08.h"
#include "HCS08SelectionDAGInfo.h"

using namespace llvm;

#define DEBUG_TYPE "hcs08-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "HCS08GenSubtargetInfo.inc"

void HCS08Subtarget::anchor() {}

HCS08Subtarget &
HCS08Subtarget::initializeSubtargetDependencies(StringRef CPU, StringRef FS) {
  StringRef CPUName = CPU;
  if (CPUName.empty())
    CPUName = "hcs08";
  ParseSubtargetFeatures(CPUName, /*TuneCPU=*/CPUName, FS);
  return *this;
}

HCS08Subtarget::HCS08Subtarget(const Triple &TT, const std::string &CPU,
                               const std::string &FS, const TargetMachine &TM)
    : HCS08GenSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), TLInfo(TM, *this),
      FrameLowering(*this) {
  TSInfo = std::make_unique<HCS08SelectionDAGInfo>();
}

HCS08Subtarget::~HCS08Subtarget() = default;

const SelectionDAGTargetInfo *HCS08Subtarget::getSelectionDAGInfo() const {
  return TSInfo.get();
}
