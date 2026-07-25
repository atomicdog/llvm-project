//===-- HCS08MCTargetDesc.cpp - HCS08 Target Descriptions ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides HCS08 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "HCS08MCTargetDesc.h"
#include "HCS08InstPrinter.h"
#include "HCS08MCAsmInfo.h"
#include "TargetInfo/HCS08TargetInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "HCS08GenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "HCS08GenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "HCS08GenRegisterInfo.inc"

static MCInstrInfo *createHCS08MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitHCS08MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createHCS08MCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitHCS08MCRegisterInfo(X, HCS08::PC);
  return X;
}

static MCAsmInfo *createHCS08MCAsmInfo(const MCRegisterInfo &MRI,
                                        const Triple &TT,
                                        const MCTargetOptions &Options) {
  return new HCS08MCAsmInfo(TT, Options);
}

static MCSubtargetInfo *
createHCS08MCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  if (CPU.empty())
    CPU = "generic";
  return createHCS08MCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

static MCInstPrinter *createHCS08MCInstPrinter(const Triple &T,
                                                unsigned SyntaxVariant,
                                                const MCAsmInfo &MAI,
                                                const MCInstrInfo &MII,
                                                const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new HCS08InstPrinter(MAI, MII, MRI);
  return nullptr;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeHCS08TargetMC() {
  Target &T = getTheHCS08Target();

  TargetRegistry::RegisterMCAsmInfo(T, createHCS08MCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createHCS08MCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createHCS08MCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createHCS08MCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createHCS08MCInstPrinter);
  TargetRegistry::RegisterMCCodeEmitter(T, createHCS08MCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createHCS08MCAsmBackend);
}
