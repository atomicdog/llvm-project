//===-- HCS08SelectionDAGInfo.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HCS08_HCS08SELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_HCS08_HCS08SELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

#define GET_SDNODE_ENUM
#include "HCS08GenSDNodeInfo.inc"

namespace llvm {

class HCS08SelectionDAGInfo : public SelectionDAGGenTargetInfo {
public:
  HCS08SelectionDAGInfo();
  ~HCS08SelectionDAGInfo() override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HCS08_HCS08SELECTIONDAGINFO_H
