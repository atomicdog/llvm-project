//===-- HCS08InstPrinter.cpp - Convert HCS08 MCInst to assembly syntax --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HCS08InstPrinter.h"
#include "HCS08MCTargetDesc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

// Include the auto-generated portion of the assembly writer.
#include "HCS08GenAsmWriter.inc"

void HCS08InstPrinter::printRegName(raw_ostream &O, MCRegister Reg) {
  O << getRegisterName(Reg);
}

void HCS08InstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                  StringRef Annot, const MCSubtargetInfo &STI,
                                  raw_ostream &O) {
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

/// Print an 8-bit value in the traditional Freescale "$nn" hex form.
void HCS08InstPrinter::printImm8(const MCInst *MI, unsigned OpNo,
                                  raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm()) {
    O << format("$%02x", static_cast<uint8_t>(Op.getImm()));
    return;
  }
  assert(Op.isExpr() && "unknown operand kind in printImm8");
  MAI.printExpr(O, *Op.getExpr());
}

/// Print a 16-bit value in the traditional Freescale "$nnnn" hex form.
void HCS08InstPrinter::printImm16(const MCInst *MI, unsigned OpNo,
                                   raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm()) {
    O << format("$%04x", static_cast<uint16_t>(Op.getImm()));
    return;
  }
  assert(Op.isExpr() && "unknown operand kind in printImm16");
  MAI.printExpr(O, *Op.getExpr());
}

/// Print a branch destination. The disassembler has already turned the encoded
/// displacement into an absolute address, so this is just a 16-bit value.
void HCS08InstPrinter::printBranchTarget(const MCInst *MI, uint64_t Address,
                                          unsigned OpNo, raw_ostream &O) {
  printImm16(MI, OpNo, O);
}
