//===-- HCS08StackToIndexed.cpp - Frame access through H:X ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Rewrites runs of frame accesses from n,sp to n,x.
//
// Every stack-relative form on this machine lives on page 2, and its page-2
// opcode byte is the same as the n,x one: "lda $03,sp" is 9E E6 03 where
// "lda $02,x" is E6 02. So the same access through H:X is a byte shorter, and
// the rewrite is exactly dropping the prefix and subtracting one from the
// displacement - tsx yields SP+1, so a frame object at SP+n is at n-1 from
// H:X.
//
// tsx costs a byte, so a run of two pays for itself and anything longer wins.
// The catch is that H:X is the only index register there is, so this is
// available only where it is dead. That sounds like nowhere, and mostly it is,
// except for the one place it matters: a 16-bit ALU chain is six frame
// accesses between the sthx that parks the operand and the ldhx that collects
// the result, and H:X is dead for all six.
//
// Two facts make the run safe to form. The frame base does not move -
// hasReservedCallFrame is true, so SP is constant through the body, and the
// pass refuses to cross anything that writes it. And tsx touches no condition
// code, which is what allows a run to span a carry chain: the whole point of
// the 16-bit chain is that C survives from the add to the adc, and inserting
// the tsx ahead of it leaves that alone.
//
//===----------------------------------------------------------------------===//

#include "HCS08.h"
#include "HCS08InstrInfo.h"
#include "HCS08Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "hcs08-stack-to-indexed"
#define PASS_NAME "HCS08 frame access through the index register"

STATISTIC(NumRewritten, "Number of n,sp frame accesses rewritten to n,x");

static cl::opt<bool>
    DisablePass("hcs08-no-stack-to-indexed", cl::Hidden,
                cl::desc("Leave HCS08 frame accesses in the n,sp form"));

namespace {

class HCS08StackToIndexed : public MachineFunctionPass {
public:
  static char ID;
  HCS08StackToIndexed() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

  StringRef getPassName() const override { return PASS_NAME; }
};

} // namespace

char HCS08StackToIndexed::ID = 0;

INITIALIZE_PASS(HCS08StackToIndexed, DEBUG_TYPE, PASS_NAME, false, false)

/// The n,x form of a frame access, or 0 if there is nothing to gain.
///
/// Missing from this list on purpose: ldhx and sthx. The 16-bit index forms are
/// themselves on page 2 - "ldhx $02,x" is 9E CE 02, no shorter than the n,sp
/// form - and sthx has no indexed form at all. cphx likewise.
static unsigned getIndexedOpcode(unsigned Opc) {
  switch (Opc) {
  case HCS08::LDAsp:  return HCS08::LDAix1;
  case HCS08::STAsp:  return HCS08::STAix1;
  case HCS08::LDXsp:  return HCS08::LDXix1;
  case HCS08::ADD8sp: return HCS08::ADD8ix1;
  case HCS08::SUB8sp: return HCS08::SUB8ix1;
  case HCS08::AND8sp: return HCS08::AND8ix1;
  case HCS08::ORA8sp: return HCS08::ORA8ix1;
  case HCS08::EOR8sp: return HCS08::EOR8ix1;
  case HCS08::ADC8sp: return HCS08::ADC8ix1;
  case HCS08::SBC8sp: return HCS08::SBC8ix1;
  case HCS08::CMP8sp: return HCS08::CMP8ix1;
  case HCS08::TSTsp:  return HCS08::TSTix1;
  case HCS08::DECsp:  return HCS08::DECix1;
  case HCS08::LSLsp:  return HCS08::LSLix1;
  case HCS08::LSRsp:  return HCS08::LSRix1;
  case HCS08::ASRsp:  return HCS08::ASRix1;
  case HCS08::ROLsp:  return HCS08::ROLix1;
  case HCS08::RORsp:  return HCS08::RORix1;
  default:
    return 0;
  }
}

/// May a run of frame accesses be carried across this instruction?
///
/// It must leave both the value in H:X and the frame base alone. Anything with
/// unmodelled side effects is turned away as well, which is what excludes the
/// push and pull pairs: they are the assembler's inherent forms, they move SP
/// under the run, and psha/pula do it without ever naming H:X.
static bool isTransparent(const MachineInstr &MI,
                          const TargetRegisterInfo &TRI) {
  if (MI.hasUnmodeledSideEffects() || MI.isCall() || MI.isInlineAsm())
    return false;
  if (MI.modifiesRegister(HCS08::SP, &TRI))
    return false;
  // A read of SP is fine - every n,sp access has one - so only writes count.
  return !MI.readsRegister(HCS08::HX, &TRI) &&
         !MI.modifiesRegister(HCS08::HX, &TRI);
}

/// The (base, displacement) pair of an n,sp operand is the last two operands of
/// every instruction that has one.
static unsigned dispOperandIndex(const MachineInstr &MI) {
  return MI.getNumExplicitOperands() - 1;
}

bool HCS08StackToIndexed::runOnMachineFunction(MachineFunction &MF) {
  const HCS08Subtarget &STI = MF.getSubtarget<HCS08Subtarget>();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();

  if (DisablePass)
    return false;

  // Whether H:X may be clobbered is the whole question, so without liveness
  // there is nothing to be done.
  if (!MF.getRegInfo().tracksLiveness())
    return false;

  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    // H:X liveness at the point just before each instruction, from a backward
    // walk over the block.
    DenseMap<const MachineInstr *, bool> DeadBefore;
    LivePhysRegs Live(TRI);
    Live.addLiveOuts(MBB);
    for (MachineInstr &MI : reverse(MBB)) {
      Live.stepBackward(MI);
      DeadBefore[&MI] = !Live.contains(HCS08::HX) &&
                        !Live.contains(HCS08::H) && !Live.contains(HCS08::X);
    }

    SmallVector<MachineInstr *, 8> Run;
    auto Flush = [&]() {
      // One access is a wash: two bytes plus the tsx against the three the
      // n,sp form already costs.
      if (Run.size() >= 2) {
        BuildMI(MBB, *Run.front(), Run.front()->getDebugLoc(),
                TII.get(HCS08::TSXd), HCS08::HX);
        for (MachineInstr *MI : Run) {
          unsigned D = dispOperandIndex(*MI);
          MI->setDesc(TII.get(getIndexedOpcode(MI->getOpcode())));
          MI->getOperand(D - 1).setReg(HCS08::HX);
          MI->getOperand(D).setImm(MI->getOperand(D).getImm() - 1);
          ++NumRewritten;
        }
        Changed = true;
      }
      Run.clear();
    };

    for (MachineInstr &MI : MBB) {
      if (!isTransparent(MI, TRI)) {
        Flush();
        continue;
      }
      if (!getIndexedOpcode(MI.getOpcode()))
        continue; // Harmless: the run carries on across it.
      // Displacement 0 has no n,x spelling - tsx lands one byte in - and an
      // outgoing argument slot is the only thing that sits there.
      if (MI.getOperand(dispOperandIndex(MI)).getImm() < 1)
        continue;
      if (!DeadBefore[&MI])
        continue;
      Run.push_back(&MI);
    }
    Flush();
  }

  return Changed;
}

FunctionPass *llvm::createHCS08StackToIndexedPass() {
  return new HCS08StackToIndexed();
}
