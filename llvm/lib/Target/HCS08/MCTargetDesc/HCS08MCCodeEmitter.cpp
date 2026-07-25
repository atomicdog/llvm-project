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

  unsigned getPCRel8OpValue(const MCInst &MI, unsigned Op,
                            SmallVectorImpl<MCFixup> &Fixups,
                            const MCSubtargetInfo &STI) const;

  /// Byte offset of the operand's encoding field from the start of the
  /// instruction.
  ///
  /// Operand bytes always occupy the tail of the encoding: a 16-bit field is
  /// the last two bytes, and where an instruction has two byte-sized fields
  /// they are the last two bytes in operand order. That covers every HCS08
  /// format, because a 16-bit field never shares an instruction with a second
  /// operand.
  unsigned getFixupOffset(const MCInst &MI, unsigned Op, unsigned Width) const {
    unsigned Size = MCII.get(MI.getOpcode()).getSize();
    if (MI.getNumOperands() == 1)
      return Size - Width / 8;
    return Size - 2 + Op;
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
  Fixups.push_back(MCFixup::create(getFixupOffset(MI, Op, 8), MO.getExpr(),
                                   HCS08::fixup_8));
  return 0;
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
