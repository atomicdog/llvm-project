//===-- HCS08Disassembler.cpp - Disassembler for HCS08 ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the HCS08Disassembler class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HCS08MCTargetDesc.h"
#include "TargetInfo/HCS08TargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoder.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;
using namespace llvm::MCD;

#define DEBUG_TYPE "hcs08-disassembler"

using DecodeStatus = MCDisassembler::DecodeStatus;

namespace {

class HCS08Disassembler : public MCDisassembler {
public:
  HCS08Disassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &MI, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};

} // end anonymous namespace

/// Recover an absolute branch target from a signed 8-bit displacement.
///
/// The displacement is relative to the end of the instruction, so the
/// instruction length has to be baked into the decoder; there is one entry
/// point per instruction size.
static DecodeStatus decodeBranchTarget(MCInst &Inst, uint64_t Imm,
                                       uint64_t Address, unsigned Size) {
  int64_t Disp = SignExtend64<8>(Imm);
  Inst.addOperand(
      MCOperand::createImm((Address + Size + Disp) & 0xFFFF));
  return MCDisassembler::Success;
}

static DecodeStatus decodeBranchTarget2(MCInst &Inst, uint64_t Imm,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  return decodeBranchTarget(Inst, Imm, Address, 2);
}

static DecodeStatus decodeBranchTarget3(MCInst &Inst, uint64_t Imm,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  return decodeBranchTarget(Inst, Imm, Address, 3);
}

static DecodeStatus decodeBranchTarget4(MCInst &Inst, uint64_t Imm,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  return decodeBranchTarget(Inst, Imm, Address, 4);
}

#include "HCS08GenDisassemblerTables.inc"

DecodeStatus HCS08Disassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                                ArrayRef<uint8_t> Bytes,
                                                uint64_t Address,
                                                raw_ostream &CStream) const {
  // An HCS08 opcode byte (together with the 0x9E page-2 prefix) determines
  // the instruction length on its own, so the fixed-length decoder tables are
  // mutually exclusive and can simply be tried shortest first.
  static const struct {
    const uint8_t *Table;
    unsigned Length;
  } Tables[] = {
      {DecoderTable8, 1},
      {DecoderTable16, 2},
      {DecoderTable24, 3},
      {DecoderTable32, 4},
  };

  for (const auto &T : Tables) {
    if (Bytes.size() < T.Length)
      break;

    uint64_t Insn = 0;
    for (unsigned I = 0; I != T.Length; ++I)
      Insn = (Insn << 8) | Bytes[I];

    MCInst Tmp;
    if (decodeInstruction(T.Table, Tmp, Insn, Address, this, STI) ==
        MCDisassembler::Success) {
      MI = Tmp;
      Size = T.Length;
      return MCDisassembler::Success;
    }
  }

  Size = 1;
  return MCDisassembler::Fail;
}

static MCDisassembler *createHCS08Disassembler(const Target &T,
                                                const MCSubtargetInfo &STI,
                                                MCContext &Ctx) {
  return new HCS08Disassembler(STI, Ctx);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeHCS08Disassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheHCS08Target(),
                                         createHCS08Disassembler);
}
