//===-- HCS08MCAsmInfo.cpp - HCS08 asm properties -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HCS08MCAsmInfo.h"

using namespace llvm;

void HCS08MCAsmInfo::anchor() {}

HCS08MCAsmInfo::HCS08MCAsmInfo(const Triple &TT,
                                 const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  CodePointerSize = 2;
  CalleeSaveStackSlotSize = 2;

  IsLittleEndian = false;

  // Traditional Freescale assembler syntax: ';' comments, '$' hex and
  // '%' binary literals.
  CommentString = ";";
  UseMotorolaIntegers = true;

  // Nothing on this target has an alignment requirement.
  AlignmentIsInBytes = true;
  UsesELFSectionDirectiveForBSS = true;

  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::None;
}
