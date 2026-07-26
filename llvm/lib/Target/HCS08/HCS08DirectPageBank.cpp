//===-- HCS08DirectPageBank.cpp - Spill slots in the direct page ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Moves spill slots out of the frame and into a shared direct-page bank.
//
// Every direct-page form is a byte shorter than the n,sp form of the same
// access, because the stack-relative forms all live on page 2: "lda $01,sp" is
// 9E E6 01 where "lda <__hcs08_dp_bank" is B6 xx. HCS08StackToIndexed already
// recovers that byte for the 8-bit accesses where H:X happens to be free, so
// what is left for the bank is the two places that pass cannot reach:
//
//   - sthx and ldhx. The 16-bit indexed forms are themselves on page 2 and
//     sthx has no indexed form at all, so a parked H:X costs three bytes each
//     way no matter what. In the page it is two.
//   - Anything at all while H:X is live. That pass needs the index register
//     dead across the whole run, which in pointer-heavy code it never is.
//
// The bank is one object shared by every function in the program, which is
// only sound because nothing promoted into it is live across a call. That is
// the rule the pass enforces below, and it is the same rule that makes the
// bank cheap: a slot that had to be saved and restored around a call would
// spend more than the byte it saves. See CodeGenDesign.md section 17.
//
// The bank itself is not emitted here. It is an undefined external symbol,
// __hcs08_dp_bank, that the linker script places in the direct page - the same
// division of labour as .page0 variables in section 15, and for the same
// reason: how much of $0000-$00FF a program may spend is a property of the
// board, not of the compiler. A bank the script does not reserve is an
// undefined-symbol error, and one it places above $00FF is an R_HCS08_8
// overflow. Neither fails quietly.
//
//===----------------------------------------------------------------------===//

#include "HCS08.h"
#include "HCS08InstrInfo.h"
#include "HCS08MachineFunctionInfo.h"
#include "HCS08Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "hcs08-dp-bank"
#define PASS_NAME "HCS08 direct-page spill bank"

STATISTIC(NumPromoted, "Number of spill slots promoted to the direct page");
STATISTIC(NumRewritten, "Number of frame accesses rewritten to direct page");

// Off by default. The direct page is the user's to spend (section 15), so
// taking any of it is a decision the compiler should not make on its own; the
// linker script has to reserve the bank before this can be turned on.
static cl::opt<unsigned> BankSize(
    "hcs08-dp-bank-size", cl::Hidden, cl::init(0),
    cl::desc("Bytes of direct page to spend on the spill bank (0 disables)"));

unsigned llvm::getHCS08DPBankSize() { return BankSize; }

namespace {

/// One instruction touching a slot, and which byte of it the access starts at.
/// A 16-bit slot is written whole by sthx and then read a byte at a time by
/// the ALU chain, so the offset is per reference rather than per slot.
struct Ref {
  MachineInstr *MI;
  unsigned Byte;
};

/// A spill slot that might be worth moving into the bank: where it sits in the
/// frame, every instruction that touches it, and what promoting it would save.
struct Candidate {
  int64_t Disp;
  unsigned Size;
  SmallVector<Ref, 8> Refs;
};

class HCS08DirectPageBank : public MachineFunctionPass {
public:
  static char ID;
  HCS08DirectPageBank() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

  StringRef getPassName() const override { return PASS_NAME; }
};

} // namespace

char HCS08DirectPageBank::ID = 0;

INITIALIZE_PASS(HCS08DirectPageBank, DEBUG_TYPE, PASS_NAME, false, false)

/// The direct-page form of a frame access, or 0 if there is none.
///
/// These four are the whole list, and they are enough: after
/// expandPostRAPseudo a spill slot is only ever loaded or stored, since the
/// 8-bit ALU column reads the *other* operand of a 16-bit chain, never the
/// parked one. A slot touched by anything else is left in the frame.
static unsigned getDirectOpcode(unsigned Opc) {
  switch (Opc) {
  case HCS08::LDAsp:  return HCS08::LDAdir;
  case HCS08::STAsp:  return HCS08::STAdir;
  case HCS08::LDHXsp: return HCS08::LDHXdir;
  case HCS08::STHXsp: return HCS08::STHXdir;
  default:
    return 0;
  }
}

/// How many bytes an n,sp access reads or writes. Anything not named here is
/// assumed to be the widest there is, so that an unrecognised access turns
/// away every slot it might overlap rather than being rewritten wrongly.
static unsigned getAccessWidth(unsigned Opc) {
  switch (Opc) {
  case HCS08::LDAsp:
  case HCS08::STAsp:
  case HCS08::ADD8sp:
  case HCS08::SUB8sp:
  case HCS08::AND8sp:
  case HCS08::ORA8sp:
  case HCS08::EOR8sp:
  case HCS08::ADC8sp:
  case HCS08::SBC8sp:
  case HCS08::CMP8sp:
  case HCS08::TSTsp:
  case HCS08::DECsp:
  case HCS08::LSLsp:
  case HCS08::LSRsp:
  case HCS08::ASRsp:
  case HCS08::ROLsp:
  case HCS08::RORsp:
  case HCS08::LDXsp:
    return 1;
  default:
    return 2;
  }
}

/// The displacement operand of an n,sp access, or nothing if this instruction
/// has none.
///
/// The base of an spmem operand is a real operand holding SP by the time this
/// runs, with the displacement immediately after it, so the pair can be found
/// without knowing which instruction this is - which is the point, since an
/// access this pass does not recognise still has to be seen.
static std::optional<unsigned> findSPDispOperand(const MachineInstr &MI) {
  for (unsigned I = 0, E = MI.getNumExplicitOperands(); I + 1 < E; ++I) {
    const MachineOperand &Base = MI.getOperand(I);
    if (Base.isReg() && Base.getReg() == HCS08::SP &&
        MI.getOperand(I + 1).isImm())
      return I + 1;
  }
  return std::nullopt;
}

/// Would a run of accesses to a promoted slot survive this instruction?
///
/// A call is the whole point: the bank is shared with the callee, so a value
/// still wanted afterwards cannot be in it. An instruction that moves SP is
/// turned away too, since every displacement this pass matched is measured
/// against an SP that has held still - which is also what excludes the push
/// and pull pairs, whose side effects are unmodelled.
static bool isTransparent(const MachineInstr &MI,
                          const TargetRegisterInfo &TRI) {
  return !MI.isCall() && !MI.isInlineAsm() && !MI.hasUnmodeledSideEffects() &&
         !MI.modifiesRegister(HCS08::SP, &TRI);
}

/// Collect every reference to one frame object and decide whether the bank can
/// hold it. Returns false the moment something disqualifies it.
static bool gatherSlot(MachineFunction &MF, const TargetRegisterInfo &TRI,
                       Candidate &C) {
  for (MachineBasicBlock &MBB : MF) {
    // Which bytes of the slot hold a value the bank could be standing in for.
    // A call resets it, because the callee is entitled to the bank: whatever
    // was in it does not survive, so a value wanted afterwards has to have
    // stayed in the frame. Entering the block resets it too, which is what
    // keeps a value from being carried in from a predecessor.
    unsigned Live = 0;

    for (MachineInstr &MI : MBB) {
      if (!isTransparent(MI, TRI)) {
        Live = 0;
        continue;
      }

      std::optional<unsigned> DispOp = findSPDispOperand(MI);
      if (!DispOp)
        continue;
      int64_t D = MI.getOperand(*DispOp).getImm();
      unsigned W = getAccessWidth(MI.getOpcode());

      if (D + int64_t(W) <= C.Disp || D >= C.Disp + int64_t(C.Size))
        continue;
      // An access has to fall wholly inside the slot to be rewritten as one
      // direct-page access. One that straddles the boundary - a word overlying
      // two objects - disqualifies it.
      if (D < C.Disp || D + int64_t(W) > C.Disp + int64_t(C.Size) ||
          !getDirectOpcode(MI.getOpcode()))
        return false;

      unsigned Byte = D - C.Disp;
      unsigned Mask = ((1u << W) - 1) << Byte;

      if (MI.mayStore()) {
        Live |= Mask;
      } else if ((Live & Mask) != Mask) {
        // Reading a byte the bank was never given: this value reached here
        // across a call or a block boundary, so it was never the bank's to
        // hold and the whole slot has to stay in the frame. Promoting only the
        // accesses that do pair up would leave the value split between two
        // places.
        return false;
      }
      C.Refs.push_back({&MI, Byte});
    }
  }

  return !C.Refs.empty();
}

bool HCS08DirectPageBank::runOnMachineFunction(MachineFunction &MF) {
  if (BankSize == 0)
    return false;

  const HCS08Subtarget &STI = MF.getSubtarget<HCS08Subtarget>();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // Spill slots only. A named local can have its address taken, and an
  // incoming argument is where the caller put it; neither is the compiler's to
  // move.
  SmallVector<Candidate, 4> Candidates;
  for (int FI = MFI.getObjectIndexBegin(), E = MFI.getObjectIndexEnd(); FI != E;
       ++FI) {
    if (MFI.isDeadObjectIndex(FI) || !MFI.isSpillSlotObjectIndex(FI))
      continue;
    uint64_t Size = MFI.getObjectSize(FI);
    if (Size != 1 && Size != 2)
      continue;
    // The same rebasing eliminateFrameIndex did, so that the displacements
    // found in the code can be matched against it.
    Candidate C;
    C.Disp = MFI.getObjectOffset(FI) + (int64_t)MFI.getStackSize() + 1;
    C.Size = Size;
    if (gatherSlot(MF, TRI, C))
      Candidates.push_back(std::move(C));
  }

  // Every rewritten access saves exactly one byte, so the best use of a small
  // bank is the slots that are touched most often.
  stable_sort(Candidates, [](const Candidate &A, const Candidate &B) {
    return A.Refs.size() > B.Refs.size();
  });

  bool Changed = false;
  auto *FuncInfo = MF.getInfo<HCS08MachineFunctionInfo>();

  for (const Candidate &C : Candidates) {
    // Instruction selection has already taken what it needed for the parked
    // operands of the 16-bit expansions, so this picks up after it.
    int Offset = FuncInfo->allocDPBank(C.Size);
    if (Offset < 0)
      continue;

    for (const Ref &R : C.Refs) {
      MachineInstr *MI = R.MI;
      MachineOperand Slot = MachineOperand::CreateES(HCS08DPBankSymbol);
      Slot.setOffset(Offset + R.Byte);

      // Operand 0 is the value in both directions - the destination of a load,
      // the source of a store - and the (base, displacement) pair it is paired
      // with goes away, along with the implicit use of SP that only the n,sp
      // form has.
      BuildMI(*MI->getParent(), *MI, MI->getDebugLoc(),
              TII.get(getDirectOpcode(MI->getOpcode())))
          .add(MI->getOperand(0))
          .add(Slot)
          .cloneMemRefs(*MI);
      MI->eraseFromParent();
      ++NumRewritten;
    }

    ++NumPromoted;
    Changed = true;
  }

  return Changed;
}

FunctionPass *llvm::createHCS08DirectPageBankPass() {
  return new HCS08DirectPageBank();
}
