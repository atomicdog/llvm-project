//===-- HCS08MCCodeEmitter.cpp - Convert HCS08 code to machine code -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the HCS08MCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HCS08FixupKinds.h"
#include "MCTargetDesc/HCS08MCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "mccodeemitter"

namespace llvm {

class HCS08MCCodeEmitter : public MCCodeEmitter {
  MCContext &Ctx;
  const MCInstrInfo &MCII;

  /// TableGen'erated function for getting the binary encoding for an
  /// instruction.
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  unsigned getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  unsigned getImm8OpValue(const MCInst &MI, unsigned Op,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const;

  unsigned getImm16OpValue(const MCInst &MI, unsigned Op,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;

  unsigned getMemDispOpValue(const MCInst &MI, unsigned Op,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;

  unsigned getMemDisp16OpValue(const MCInst &MI, unsigned Op,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const;

  unsigned getPCRel8OpValue(const MCInst &MI, unsigned Op,
                            SmallVectorImpl<MCFixup> &Fixups,
                            const MCSubtargetInfo &STI) const;

  /// Byte offset of the operand's encoding field from the start of the
  /// instruction.
  ///
  /// Encoded fields always occupy the tail: a 16-bit field is the last two
  /// bytes and never shares an instruction with another field, and where there
  /// are two byte-sized fields they are the last two bytes in operand order.
  ///
  /// The position has to be counted over the *encoded* operands rather than
  /// over all of them. A code-generation form carries register operands that
  /// contribute nothing to the encoding - "lda #$nn" is written with a
  /// destination register the assembler's own form does not have - so operand
  /// index and byte position are not the same thing.
  unsigned getFixupOffset(const MCInst &MI, unsigned Op, unsigned Width) const {
    unsigned Size = MCII.get(MI.getOpcode()).getSize();
    if (Width == 16)
      return Size - 2;

    unsigned Index = 0, Total = 0;
    for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
      const MCOperand &MO = MI.getOperand(I);
      if (!MO.isImm() && !MO.isExpr())
        continue;
      if (I < Op)
        ++Index;
      ++Total;
    }
    assert(Total >= 1 && Total <= 2 && "unexpected number of encoded fields");
    return Size - Total + Index;
  }

public:
  HCS08MCCodeEmitter(MCContext &Ctx, const MCInstrInfo &MCII)
      : Ctx(Ctx), MCII(MCII) {}

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;
};

void HCS08MCCodeEmitter::encodeInstruction(const MCInst &MI,
                                            SmallVectorImpl<char> &CB,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  const MCInstrDesc &Desc = MCII.get(MI.getOpcode());
  unsigned Size = Desc.getSize();
  assert(Size >= 1 && Size <= 4 && "invalid HCS08 instruction size");

  uint64_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);

  // The first byte emitted occupies the most significant bits of the encoding.
  for (unsigned I = 0; I != Size; ++I)
    CB.push_back(static_cast<char>((Bits >> (8 * (Size - 1 - I))) & 0xFF));
}

unsigned
HCS08MCCodeEmitter::getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  if (MO.isImm())
    return static_cast<unsigned>(MO.getImm());
  llvm_unreachable("unhandled operand kind");
}

unsigned HCS08MCCodeEmitter::getImm8OpValue(const MCInst &MI, unsigned Op,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(Op);
  if (MO.isImm())
    return MO.getImm() & 0xFF;

  assert(MO.isExpr() && "expected immediate or expression");

  // A '#>expr' / '#<expr' byte selector arrives as an MCSpecifierExpr; the
  // selected byte is encoded through a hi8/lo8 fixup on the inner expression.
  const MCExpr *Expr = MO.getExpr();
  HCS08::Fixups Kind = HCS08::fixup_8;
  if (const auto *SE = dyn_cast<MCSpecifierExpr>(Expr)) {
    switch (SE->getSpecifier()) {
    case HCS08::S_HI8:
      Kind = HCS08::fixup_hi8;
      Expr = SE->getSubExpr();
      break;
    case HCS08::S_LO8:
      Kind = HCS08::fixup_lo8;
      Expr = SE->getSubExpr();
      break;
    default:
      break;
    }
  }

  Fixups.push_back(MCFixup::create(getFixupOffset(MI, Op, 8), Expr, Kind));
  return 0;
}

/// Encode an n,sp or n,x operand. The operand pair is (base, displacement);
/// the base is the register the addressing mode implies, so only the
/// displacement is encoded. Frame offsets are always resolved to constants by
/// the time they get here.
unsigned
HCS08MCCodeEmitter::getMemDispOpValue(const MCInst &MI, unsigned Op,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(Op + 1);
  assert(MO.isImm() && "expected a resolved displacement");
  return MO.getImm() & 0xFF;
}

/// Encode the 16-bit displacement of an nn,x operand.
unsigned
HCS08MCCodeEmitter::getMemDisp16OpValue(const MCInst &MI, unsigned Op,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(Op + 1);
  assert(MO.isImm() && "expected a resolved displacement");
  return MO.getImm() & 0xFFFF;
}

unsigned
HCS08MCCodeEmitter::getImm16OpValue(const MCInst &MI, unsigned Op,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(Op);
  if (MO.isImm())
    return MO.getImm() & 0xFFFF;

  assert(MO.isExpr() && "expected immediate or expression");
  Fixups.push_back(MCFixup::create(getFixupOffset(MI, Op, 16), MO.getExpr(),
                                   HCS08::fixup_16));
  return 0;
}

unsigned
HCS08MCCodeEmitter::getPCRel8OpValue(const MCInst &MI, unsigned Op,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(Op);

  // A resolved branch operand is an absolute target address, so the assembler
  // still has to go through a fixup to turn it into a displacement.
  const MCExpr *Expr =
      MO.isImm() ? MCConstantExpr::create(MO.getImm(), Ctx) : MO.getExpr();

  Fixups.push_back(MCFixup::create(getFixupOffset(MI, Op, 8), Expr,
                                   HCS08::fixup_pcrel_8, /*PCRel=*/true));
  return 0;
}

MCCodeEmitter *createHCS08MCCodeEmitter(const MCInstrInfo &MCII,
                                         MCContext &Ctx) {
  return new HCS08MCCodeEmitter(Ctx, MCII);
}

#include "HCS08GenMCCodeEmitter.inc"

} // end namespace llvm
