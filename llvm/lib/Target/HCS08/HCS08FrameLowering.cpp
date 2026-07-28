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
static void adjustSP(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                     const DebugLoc &DL, const TargetInstrInfo &TII,
                     int64_t Amount) {
  while (Amount != 0) {
    int64_t Step = Amount < 0 ? std::max<int64_t>(Amount, -128)
                              : std::min<int64_t>(Amount, 127);
    BuildMI(MBB, MBBI, DL, TII.get(HCS08::AISi)).addImm(Step);
    Amount -= Step;
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
  // only for ordinary functions.
  if (!IsInterrupt && StackSize == 0)
    return;

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

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

    // A and the condition codes the hardware did stack, so the transfer
    // register and the flags that lda sets are both free to use here.
    for (unsigned Byte = 0, N = bankBytesToSave(MF); Byte != N; ++Byte) {
      BuildMI(MBB, MBBI, DL, TII.get(HCS08::LDAdir), HCS08::A).add(bankSlot(Byte));
      BuildMI(MBB, MBBI, DL, TII.get(HCS08::PSHA))
          .addReg(HCS08::A, RegState::Implicit);
    }
  }

  // Last, so that everything above it lands outside the frame: n,sp
  // displacements are measured from where SP ends up, and a push after this
  // point would move SP out from under every one of them.
  adjustSP(MBB, MBBI, DL, TII, -StackSize);
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

  // Undo the frame first, so what the prologue pushed is back on top.
  adjustSP(MBB, MBBI, DL, TII, StackSize);

  if (IsInterrupt) {
    // Pops mirror pushes, so this counts down where the prologue counted up.
    for (unsigned Byte = bankBytesToSave(MF); Byte-- > 0;) {
      BuildMI(MBB, MBBI, DL, TII.get(HCS08::PULA))
          .addReg(HCS08::A, RegState::Define | RegState::Implicit);
      BuildMI(MBB, MBBI, DL, TII.get(HCS08::STAdir))
          .addReg(HCS08::A)
          .add(bankSlot(Byte));
    }

    // The rti this runs ahead of restores the rest.
    BuildMI(MBB, MBBI, DL, TII.get(HCS08::PULH))
        .addReg(HCS08::H, RegState::Define | RegState::Implicit);
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
