//===-- HCS08MCAsmInfo.cpp - HCS08 asm properties -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HCS08MCAsmInfo.h"
#include "HCS08MCTargetDesc.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

void HCS08MCAsmInfo::anchor() {}

void HCS08MCAsmInfo::printSpecifierExpr(raw_ostream &OS,
                                        const MCSpecifierExpr &Expr) const {
  switch (Expr.getSpecifier()) {
  case HCS08::S_HI8:
    OS << '>';
    break;
  case HCS08::S_LO8:
    OS << '<';
    break;
  default:
    break;
  }
  printExpr(OS, *Expr.getSubExpr());
}

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
