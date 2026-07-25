//===-- HCS08MachineFunctionInfo.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// HCS08-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HCS08_HCS08MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_HCS08_HCS08MACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class HCS08MachineFunctionInfo : public MachineFunctionInfo {
  virtual void anchor();

public:
  HCS08MachineFunctionInfo() = default;
  HCS08MachineFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HCS08_HCS08MACHINEFUNCTIONINFO_H
