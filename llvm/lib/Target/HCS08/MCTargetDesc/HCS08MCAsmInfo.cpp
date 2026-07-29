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

  // One, and this is not a rounding of the 16-bit pointer down. It is the
  // DWARF data alignment factor - the unit .cfi_offset displacements are
  // counted in - and it is the only thing this field feeds. Every push on this
  // machine is one byte wide (pshh, psha, pshx; there is no 16-bit push), the
  // stack is byte-aligned, and an interrupt frame is an odd five bytes, so a
  // factor of 2 would leave half the saved registers unable to use the compact
  // DW_CFA_offset form and describe none of them more accurately.
  CalleeSaveStackSlotSize = 1;

  IsLittleEndian = false;

  // Traditional Freescale assembler syntax: ';' comments, '$' hex and
  // '%' binary literals.
  CommentString = ";";
  UseMotorolaIntegers = true;

  // Nothing on this target has an alignment requirement.
  AlignmentIsInBytes = true;
  UsesELFSectionDirectiveForBSS = true;

  SupportsDebugInformation = true;

  // There is no unwinder here and nothing to throw with, so exceptions stay
  // off - but the two questions are separate, and this is the pair of settings
  // that asks for CFI in .debug_frame without .eh_frame coming with it. A
  // debugger needs the same frame description an unwinder would to walk out of
  // the function it halted in, and .debug_frame is not an allocated section,
  // so describing every instruction boundary costs nothing in a 32KB part.
  ExceptionsType = ExceptionHandling::None;
  UsesCFIWithoutEH = true;
}
