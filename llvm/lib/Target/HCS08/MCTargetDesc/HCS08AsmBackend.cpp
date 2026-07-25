//===-- HCS08AsmBackend.cpp - HCS08 Assembler Backend -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HCS08FixupKinds.h"
#include "MCTargetDesc/HCS08MCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class HCS08AsmBackend : public MCAsmBackend {
  uint8_t OSABI;

public:
  HCS08AsmBackend(const MCSubtargetInfo &STI, uint8_t OSABI)
      : MCAsmBackend(llvm::endianness::big), OSABI(OSABI) {}

  void applyFixup(const MCFragment &, const MCFixup &, const MCValue &Target,
                  uint8_t *Data, uint64_t Value, bool IsResolved) override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createHCS08ELFObjectWriter(OSABI);
  }

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    // Must be kept in the same order as the enum in HCS08FixupKinds.h.
    const static MCFixupKindInfo Infos[HCS08::NumTargetFixupKinds] = {
        // name            offset bits flags
        {"fixup_8", 0, 8, 0},
        {"fixup_16", 0, 16, 0},
        {"fixup_pcrel_8", 0, 8, 0},
    };
    static_assert(std::size(Infos) == HCS08::NumTargetFixupKinds,
                  "Not all fixup kinds added to Infos array");

    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);
    return Infos[Kind - FirstTargetFixupKind];
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override {
    // NOP is a single byte and nothing on this target needs alignment, so any
    // requested count can be filled exactly.
    for (uint64_t I = 0; I != Count; ++I)
      OS << '\x9D';
    return true;
  }
};

void HCS08AsmBackend::applyFixup(const MCFragment &F, const MCFixup &Fixup,
                                  const MCValue &Target, uint8_t *Data,
                                  uint64_t Value, bool IsResolved) {
  maybeAddReloc(F, Fixup, Target, Value, IsResolved);
  if (!IsResolved)
    return;

  MCContext &Ctx = getContext();
  unsigned Kind = Fixup.getKind();
  unsigned NumBytes = 0;

  switch (Kind) {
  default:
    if (Kind < FirstTargetFixupKind) {
      // Generic data fixups (.byte/.word directives) are plain big-endian
      // stores of the given width.
      NumBytes = getFixupKindInfo(Fixup.getKind()).TargetSize / 8;
      break;
    }
    llvm_unreachable("unhandled HCS08 fixup kind");

  case HCS08::fixup_8:
    if (!isUInt<8>(Value) && !isInt<8>(static_cast<int64_t>(Value)))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range for a byte");
    NumBytes = 1;
    break;

  case HCS08::fixup_16:
    if (!isUInt<16>(Value) && !isInt<16>(static_cast<int64_t>(Value)))
      Ctx.reportError(Fixup.getLoc(), "fixup value out of range for a word");
    NumBytes = 2;
    break;

  case HCS08::fixup_pcrel_8: {
    // The displacement is measured from the end of the instruction, and the
    // fixup sits in the final byte, so the PC-relative base is one past it.
    int64_t Disp = static_cast<int64_t>(Value) - 1;
    if (!isInt<8>(Disp))
      Ctx.reportError(Fixup.getLoc(), "branch target out of range");
    Value = static_cast<uint64_t>(Disp);
    NumBytes = 1;
    break;
  }
  }

  // Multi-byte fields are stored big endian, most significant byte first.
  for (unsigned I = 0; I != NumBytes; ++I)
    Data[I] |= static_cast<uint8_t>((Value >> (8 * (NumBytes - 1 - I))) & 0xFF);
}

} // end anonymous namespace

MCAsmBackend *llvm::createHCS08MCAsmBackend(const Target &T,
                                             const MCSubtargetInfo &STI,
                                             const MCRegisterInfo &MRI,
                                             const MCTargetOptions &Options) {
  return new HCS08AsmBackend(STI, ELF::ELFOSABI_STANDALONE);
}
