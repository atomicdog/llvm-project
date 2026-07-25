//===-- HCS08TargetMachine.h - Define TargetMachine for HCS08 -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The HCS08 target machine.
//
// Code generation is not implemented. This exists so that the target is
// reachable through the normal TargetMachine plumbing (which is also what
// causes TableGen to run for the target) and so that tools can ask for the
// HCS08 data layout. Anything that tries to emit code through it will fail
// for lack of a subtarget; the register model, calling convention and
// GlobalISel pipeline are the next milestone.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HCS08_HCS08TARGETMACHINE_H
#define LLVM_LIB_TARGET_HCS08_HCS08TARGETMACHINE_H

#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include "llvm/Target/TargetMachine.h"
#include <memory>
#include <optional>

namespace llvm {

class TargetLoweringObjectFile;

class HCS08TargetMachine : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;

public:
  HCS08TargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                      StringRef FS, const TargetOptions &Options,
                      std::optional<Reloc::Model> RM,
                      std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                      bool JIT);
  ~HCS08TargetMachine() override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HCS08_HCS08TARGETMACHINE_H
