//===-- HCS08ELFObjectWriter.cpp - HCS08 ELF Writer ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HCS08FixupKinds.h"
#include "MCTargetDesc/HCS08MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {

class HCS08ELFObjectWriter : public MCELFObjectTargetWriter {
public:
  explicit HCS08ELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(/*Is64Bit=*/false, OSABI, ELF::EM_68HC08,
                                /*HasRelocationAddend=*/true) {}

protected:
  unsigned getRelocType(const MCFixup &Fixup, const MCValue &,
                        bool IsPCRel) const override {
    switch (Fixup.getKind()) {
    case FK_Data_1:
      return ELF::R_HCS08_8;
    case FK_Data_2:
      return ELF::R_HCS08_16;
    case HCS08::fixup_8:
      return ELF::R_HCS08_8;
    case HCS08::fixup_16:
      return ELF::R_HCS08_16;
    case HCS08::fixup_pcrel_8:
      return ELF::R_HCS08_PCREL_8;
    default:
      llvm_unreachable("invalid fixup kind");
    }
  }
};

} // end anonymous namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createHCS08ELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<HCS08ELFObjectWriter>(OSABI);
}
