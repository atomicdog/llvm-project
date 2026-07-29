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
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCDwarf.h"
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
  MCAsmInfo *MAI = new HCS08MCAsmInfo(TT, Options);

  // The CIE's initial rule: what is true at the first instruction of an
  // ordinary function, before its prologue has run.
  //
  // The CFA is SP as the caller had it at the call site, which is DWARF's own
  // "typical" definition and the one to take here, because it is the only one
  // an unwinder gets for free. Recovering the caller's SP is what lets the
  // *next* frame's rule - also written against SP - be evaluated at all, and
  // no consumer asks: libunwind assigns the CFA to SP unconditionally
  // (DwarfInstructions.hpp), and gdb's ports default the SP rule to the CFA.
  // Any other choice would need an explicit DW_CFA_val_offset for SP that
  // those two would ignore, and ignoring it walks every parent frame off by
  // the difference, silently and cumulatively.
  //
  // A jsr has since pushed two bytes, so the CFA is SP+2 here.
  MAI->addInitialFrameState(MCCFIInstruction::cfiDefCfa(
      nullptr, MRI.getDwarfRegNum(HCS08::SP, true), 2));

  // The return address straddles the CFA rather than sitting below it: -1, not
  // the -2 that the same reasoning gives on most machines. That is the whole
  // consequence of SP pointing one byte *below* the last thing pushed here.
  // The jsr pushed PCL first, at the CFA itself, then PCH below it, and the
  // two bytes are read high-first like every multi-byte field on this target,
  // so the address is CFA-1.
  MAI->addInitialFrameState(MCCFIInstruction::createOffset(
      nullptr, MRI.getDwarfRegNum(HCS08::PC, true), -1));

  return MAI;
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
