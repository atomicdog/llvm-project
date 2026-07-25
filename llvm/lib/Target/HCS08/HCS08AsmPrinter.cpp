//===-- HCS08AsmPrinter.cpp - HCS08 LLVM assembly writer -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Prints HCS08 machine code to the HCS08 assembly language.
//
//===----------------------------------------------------------------------===//

#include "HCS08.h"
#include "HCS08MCInstLower.h"
#include "MCTargetDesc/HCS08InstPrinter.h"
#include "TargetInfo/HCS08TargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {
class HCS08AsmPrinter : public AsmPrinter {
public:
  HCS08AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "HCS08 Assembly Printer"; }

  void printOperand(const MachineInstr *MI, int OpNum, raw_ostream &O);
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &O) override;
  void emitInstruction(const MachineInstr *MI) override;

  static char ID;
};
} // namespace

void HCS08AsmPrinter::printOperand(const MachineInstr *MI, int OpNum,
                                   raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(OpNum);
  switch (MO.getType()) {
  default:
    llvm_unreachable("Not implemented yet!");
  case MachineOperand::MO_Register:
    O << HCS08InstPrinter::getRegisterName(MO.getReg());
    return;
  case MachineOperand::MO_Immediate:
    O << MO.getImm();
    return;
  case MachineOperand::MO_GlobalAddress:
    getSymbol(MO.getGlobal())->print(O, MAI);
    return;
  }
}

bool HCS08AsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                      const char *ExtraCode, raw_ostream &O) {
  if (ExtraCode && ExtraCode[0])
    return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, O);
  printOperand(MI, OpNo, O);
  return false;
}

void HCS08AsmPrinter::emitInstruction(const MachineInstr *MI) {
  // A pseudo that reaches here has no encoding and an empty asm string, so it
  // would print as a blank line and silently drop whatever it stood for. This
  // target leans on pseudos that later passes expand, so refuse loudly rather
  // than emit nothing. (The generic pseudos - DBG_*, KILL, IMPLICIT_DEF and
  // friends - are handled by AsmPrinter before it gets here.)
  if (MI->isPseudo()) {
    const TargetInstrInfo &TII = *MF->getSubtarget().getInstrInfo();
    report_fatal_error("HCS08 pseudo-instruction survived to the asm printer: " +
                       TII.getName(MI->getOpcode()));
  }

  HCS08MCInstLower MCInstLowering(OutContext, *this);
  MCInst TmpInst;
  MCInstLowering.Lower(MI, TmpInst);
  EmitToStreamer(*OutStreamer, TmpInst);
}

char HCS08AsmPrinter::ID = 0;

INITIALIZE_PASS(HCS08AsmPrinter, "hcs08-asm-printer", "HCS08 Assembly Printer",
                false, false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeHCS08AsmPrinter() {
  RegisterAsmPrinter<HCS08AsmPrinter> X(getTheHCS08Target());
}
