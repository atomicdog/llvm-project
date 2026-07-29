//===-- HCS08FrameLowering.cpp - HCS08 Frame Lowering --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HCS08FrameLowering.h"
#include "HCS08.h"
#include "HCS08InstrInfo.h"
#include "HCS08MachineFunctionInfo.h"
#include "HCS08Subtarget.h"
#include "llvm/CodeGen/CFIInstBuilder.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

HCS08FrameLowering::HCS08FrameLowering(const HCS08Subtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown,
                          /*StackAlignment=*/Align(1),
                          /*LocalAreaOffset=*/0) {}

bool HCS08FrameLowering::hasFPImpl(const MachineFunction &MF) const {
  return false;
}

/// Move SP by Amount, which ais can only do a signed byte at a time.
///
/// A frame bigger than that takes more than one, which is two bytes each and
/// still cheaper than the tsx/aix/txs round trip - aix takes a signed byte too,
/// so that would need just as many and clobber H:X besides. The asymmetry is
/// real and worth spelling out: allocating can step -128 but freeing can only
/// step +127, so the two directions do not always use the same count.
///
/// CFI, when there is any, is emitted after each step rather than once at the
/// end: SP moves on every one of them and the CFA must not, so the distance
/// between the two changes by the same amount in the other direction. A frame
/// over 127 bytes takes several ais, and describing each keeps the unwind rule
/// true at every instruction boundary instead of only once the run finishes.
static void adjustSP(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                     const DebugLoc &DL, const TargetInstrInfo &TII,
                     int64_t Amount, CFIInstBuilder *CFI,
                     int64_t &CFAOffset) {
  while (Amount != 0) {
    int64_t Step = Amount < 0 ? std::max<int64_t>(Amount, -128)
                              : std::min<int64_t>(Amount, 127);
    BuildMI(MBB, MBBI, DL, TII.get(HCS08::AISi)).addImm(Step);
    Amount -= Step;
    CFAOffset -= Step;
    if (CFI)
      CFI->buildDefCFAOffset(CFAOffset);
  }
}

/// Is this function an interrupt handler - entered by the interrupt sequence
/// rather than by a jsr, and left with rti?
static bool isInterruptHandler(const MachineFunction &MF) {
  return MF.getFunction().hasFnAttribute("hcs08-interrupt");
}

/// Bytes of the direct-page bank a handler has to preserve.
///
/// The bank is one object shared by the whole program, and it is sound only
/// because nothing in it is live across a call (section 17). An interrupt is
/// not a call: it can land between the two halves of a promoted sequence, so a
/// handler that reaches any banked code has to put back what it found.
///
/// Whether it does at all is decided by __attribute__((no_direct_page_bank)),
/// which getHCS08DPBankSize answers zero for - the same answer it gives when
/// the bank is switched off altogether, which is why there is no second
/// condition for it here.
///
/// How much splits on whether the handler calls anything. A call goes to code
/// this function cannot see, which may use the bank up to the size the whole
/// program agreed on, so a non-leaf handler has to assume all of it. A leaf
/// handler uses only what it allocated itself, and that count is final by now
/// because HCS08DirectPageBank declines to run on handlers for exactly this
/// reason - so the common ISR, which is a leaf and touches no bank, saves
/// nothing and costs the pshh/pulh pair alone.
static unsigned bankBytesToSave(const MachineFunction &MF) {
  if (!isInterruptHandler(MF))
    return 0;
  unsigned BankSize = getHCS08DPBankSize(MF.getFunction());
  if (MF.getFrameInfo().hasCalls())
    return BankSize;
  return std::min(MF.getInfo<HCS08MachineFunctionInfo>()->getDPBankUsed(),
                  BankSize);
}

/// How far above SP the CFA sits at the function's first instruction.
///
/// The CFA is SP as the caller had it at the call site (see the CIE's initial
/// rule in HCS08MCTargetDesc.cpp), so this is simply what the entry sequence
/// pushed on the way in. A jsr pushes a two-byte return address, giving the 2
/// the CIE already states and which an ordinary function therefore never has
/// to repeat. The interrupt sequence pushes five bytes - return address, X, A
/// and the condition codes - so a handler starts three further down and has to
/// say so before anything else.
static int64_t entryCFAOffset(const MachineFunction &MF) {
  return isInterruptHandler(MF) ? 5 : 2;
}

/// Bytes this function's prologue pushes itself, on top of what the entry
/// sequence already did - the pshh and the bank, both of them a handler's.
/// Only the epilogue needs this, to know where the CFA started from.
static int64_t prologuePushBytes(const MachineFunction &MF) {
  if (!isInterruptHandler(MF))
    return 0;
  return 1 + bankBytesToSave(MF);
}

/// One byte of the bank, as an operand the linker resolves.
static MachineOperand bankSlot(unsigned Byte) {
  MachineOperand Slot = MachineOperand::CreateES(HCS08DPBankSymbol);
  Slot.setOffset(Byte);
  return Slot;
}

void HCS08FrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  int64_t StackSize = MF.getFrameInfo().getStackSize();
  bool IsInterrupt = isInterruptHandler(MF);

  // A handler has entry code to emit even with no frame, so the early-out is
  // only for ordinary functions. There is nothing to describe either: a
  // function that moves neither SP nor a register keeps the CIE's rule from
  // its first instruction to its last.
  if (!IsInterrupt && StackSize == 0)
    return;

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  // Only under -g. These go to .debug_frame, and with exceptions off there is
  // no .eh_frame wanting them for any other reason.
  bool NeedsCFI = MF.needsFrameMoves();
  CFIInstBuilder CFI(MBB, MBBI, MachineInstr::FrameSetup);

  // Distance from SP to the CFA, which every rule below is measured against.
  // It starts at what the entry sequence pushed and grows by one for each byte
  // this prologue pushes on top of that.
  int64_t CFAOffset = entryCFAOffset(MF);

  if (IsInterrupt) {
    // Nothing calls a handler, so there is nowhere for arguments to come from.
    // It matters beyond tidiness: an incoming stack argument is addressed at
    // getStackSize() + 2, counting the return address a jsr pushed, and an
    // interrupt frame is five bytes rather than two - so an argument would be
    // read from the wrong place. Clang rejects this; llc and hand-written IR
    // reach here instead.
    if (!MF.getFunction().arg_empty())
      report_fatal_error("HCS08 interrupt handler '" + MF.getName() +
                         "' cannot take arguments");

    // Correct the CIE's rule before anything moves: the hardware pushed five
    // bytes where a jsr pushes two, so the CFA is three further from SP than
    // the CIE says and three more bytes of the interrupted function are on the
    // stack. Where the CFA *is* does not change - it is still SP as the
    // interrupted function had it - so the return address rule the CIE gives
    // is still right and only the distance has to be restated.
    //
    // The byte pushed when the distance became N ends up at CFA-(N-1), which
    // walking the sequence gives as PCL at the CFA itself, PCH at CFA-1, then
    // X at CFA-2, A at CFA-3 and the condition codes at CFA-4. The CCR is the
    // one byte here that cannot be described, because LLVM models it as the
    // two halves NZV and C (section 6) and neither of them names the whole
    // register. Nothing unwinds through the condition codes, so nothing is
    // lost by that.
    if (NeedsCFI) {
      CFI.buildDefCFAOffset(CFAOffset);
      CFI.buildOffset(HCS08::X, -2);
      CFI.buildOffset(HCS08::A, -3);
    }

    // The interrupt sequence stacks the return address, X, A and the condition
    // codes - but not H, which the CPU08 core added after the frame layout was
    // fixed. H:X is this target's pointer register and its 16-bit accumulator,
    // so almost any handler clobbers H, and losing it corrupts the code that
    // was interrupted rather than this one. Verified on ucsim 2026-07-27.
    //
    // H arrives holding the interrupted function's value, which is the whole
    // reason to push it, so it is live in even where this handler never reads
    // it - without that the verifier sees pshh use an undefined register.
    MBB.addLiveIn(HCS08::H);
    BuildMI(MBB, MBBI, DL, TII.get(HCS08::PSHH))
        .addReg(HCS08::H, RegState::Implicit);
    ++CFAOffset;
    if (NeedsCFI) {
      CFI.buildDefCFAOffset(CFAOffset);
      CFI.buildOffset(HCS08::H, 1 - CFAOffset);
    }

    // A and the condition codes the hardware did stack, so the transfer
    // register and the flags that lda sets are both free to use here.
    for (unsigned Byte = 0, N = bankBytesToSave(MF); Byte != N; ++Byte) {
      BuildMI(MBB, MBBI, DL, TII.get(HCS08::LDAdir), HCS08::A).add(bankSlot(Byte));
      BuildMI(MBB, MBBI, DL, TII.get(HCS08::PSHA))
          .addReg(HCS08::A, RegState::Implicit);
      // The CFA moves, but there is no rule to write for what landed on the
      // stack: a byte of the direct-page bank is memory, not a register, and
      // DWARF has no way to say a memory location was saved. Putting it back
      // is the epilogue's job and no unwinder's.
      ++CFAOffset;
      if (NeedsCFI)
        CFI.buildDefCFAOffset(CFAOffset);
    }
  }

  // Last, so that everything above it lands outside the frame: n,sp
  // displacements are measured from where SP ends up, and a push after this
  // point would move SP out from under every one of them.
  adjustSP(MBB, MBBI, DL, TII, -StackSize, NeedsCFI ? &CFI : nullptr,
           CFAOffset);
}

void HCS08FrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  int64_t StackSize = MF.getFrameInfo().getStackSize();
  bool IsInterrupt = isInterruptHandler(MF);
  if (!IsInterrupt && StackSize == 0)
    return;

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  // The epilogue is described as carefully as the prologue, which is not the
  // usual economy and is the right trade here. .debug_frame is not an
  // allocated section, so accuracy costs nothing on a 32KB part; and the
  // reason this target carries debug info at all is halting at an arbitrary PC
  // over BDM, which can land on one of these instructions as easily as any
  // other. Undescribed, they would report the caller of whatever the frame
  // used to be.
  bool NeedsCFI = MF.needsFrameMoves();
  CFIInstBuilder CFI(MBB, MBBI, MachineInstr::FrameDestroy);
  int64_t CFAOffset = entryCFAOffset(MF) + prologuePushBytes(MF) + StackSize;

  // Undo the frame first, so what the prologue pushed is back on top.
  adjustSP(MBB, MBBI, DL, TII, StackSize, NeedsCFI ? &CFI : nullptr,
           CFAOffset);

  if (IsInterrupt) {
    // Pops mirror pushes, so this counts down where the prologue counted up.
    for (unsigned Byte = bankBytesToSave(MF); Byte-- > 0;) {
      BuildMI(MBB, MBBI, DL, TII.get(HCS08::PULA))
          .addReg(HCS08::A, RegState::Define | RegState::Implicit);
      BuildMI(MBB, MBBI, DL, TII.get(HCS08::STAdir))
          .addReg(HCS08::A)
          .add(bankSlot(Byte));
      --CFAOffset;
      if (NeedsCFI)
        CFI.buildDefCFAOffset(CFAOffset);
    }

    // The rti this runs ahead of restores the rest.
    BuildMI(MBB, MBBI, DL, TII.get(HCS08::PULH))
        .addReg(HCS08::H, RegState::Define | RegState::Implicit);
    --CFAOffset;
    if (NeedsCFI) {
      CFI.buildDefCFAOffset(CFAOffset);
      // H is back to holding the interrupted function's own value, so the rule
      // saying where to find it on the stack has to stop applying. What is
      // left is the entry state exactly: the CFA five above SP, with X and A
      // still stacked for the rti to take back.
      CFI.buildRestore(HCS08::H);
    }
  }
}

void HCS08FrameLowering::resetCFIToInitialState(MachineBasicBlock &MBB) const {
  const MachineFunction &MF = *MBB.getParent();
  CFIInstBuilder CFI(MBB, MBB.begin(), MachineInstr::NoFlags);

  // Back to what held at the function's first instruction. For an ordinary
  // function that is the CIE's rule and nothing else, since its prologue
  // touches no register - only the CFA has to be restated. A handler entered
  // with three registers already stacked by the hardware, so those rules are
  // part of its entry state too, and the pshh its own prologue added is not.
  CFI.buildDefCFA(HCS08::SP, entryCFAOffset(MF));
  if (isInterruptHandler(MF)) {
    CFI.buildOffset(HCS08::X, -2);
    CFI.buildOffset(HCS08::A, -3);
    CFI.buildRestore(HCS08::H);
  }
}

MachineBasicBlock::iterator HCS08FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  // The call frame is reserved, so the markers just disappear.
  return MBB.erase(I);
}

bool HCS08FrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return true;
}

StackOffset
HCS08FrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                           Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  FrameReg = HCS08::SP;

  // The same rebasing HCS08RegisterInfo::eliminateFrameIndex does, and it has
  // to stay the same: this is what debug info reports as a variable's address,
  // and eliminateFrameIndex is what the code actually uses.
  //
  // The +1 is the whole point. SP points one byte *below* the last thing
  // pushed, so the lowest byte of the frame is 1,sp. Inheriting the default,
  // which stops at object offset plus frame size, describes every local one
  // byte low - a debugger then reads the high half of one variable and the low
  // half of its neighbour, and prints a plausible wrong number rather than
  // failing.
  return StackOffset::getFixed(MFI.getObjectOffset(FI) +
                               (int64_t)MFI.getStackSize() + 1);
}
