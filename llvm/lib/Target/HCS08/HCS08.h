//===-- HCS08.h - Top-level interface for HCS08 ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HCS08_HCS08_H
#define LLVM_LIB_TARGET_HCS08_HCS08_H

#include "MCTargetDesc/HCS08MCTargetDesc.h"
#include "llvm/Support/CodeGen.h"

namespace HCS08CC {
// HCS08 branch condition codes. The value is the operand of a HCS08ISD::BR_CC
// node and selects the concrete conditional branch instruction.
enum CondCode {
  COND_EQ = 0, // beq
  COND_NE = 1, // bne
  COND_HS = 2, // bcc  (unsigned >=)
  COND_LO = 3, // bcs  (unsigned <)
  COND_HI = 4, // bhi  (unsigned >)
  COND_LS = 5, // bls  (unsigned <=)
  COND_GE = 6, // bge  (signed >=)
  COND_LT = 7, // blt  (signed <)
  COND_GT = 8, // bgt  (signed >)
  COND_LE = 9, // ble  (signed <=)
  COND_MI = 10, // bmi
  COND_PL = 11, // bpl
  COND_INVALID = -1
};
}

namespace llvm {
class FunctionPass;
class Function;
class HCS08TargetMachine;
class PassRegistry;

FunctionPass *createHCS08ISelDag(HCS08TargetMachine &TM,
                                 CodeGenOptLevel OptLevel);

/// Bytes an indirect call reserves at the bottom of its outgoing arguments:
/// the target it jumps to with an rts, and the address to come back to. See
/// LowerCall for the layout and HCS08InstrInfo.td for why it is an rts.
inline constexpr int HCS08IndirectCallBlockSize = 4;

/// The direct-page spill bank. The compiler refers to this symbol and never
/// defines it: how much of $0000-$00FF a program may spend is a property of the
/// board, so the linker script places it. See CodeGenDesign.md section 17.
inline constexpr char HCS08DPBankSymbol[] = "__hcs08_dp_bank";

/// Bytes of direct page `F` may spend on the bank, 0 if it is switched off.
///
/// The number comes from clang's -mdirect-page-bank= as a function attribute,
/// because how much of the page is free is a property of the board and not of
/// the program. -hcs08-dp-bank-size overrides it, for llc and for tests that
/// have no clang to set an attribute.
unsigned getHCS08DPBankSize(const Function &F);

/// Holds a conditional branch together with the instruction that set the flags
/// it reads, so that register allocation cannot insert a reload between them.
FunctionPass *createHCS08FuseCompareBranchPass();

/// Rewrites runs of n,sp frame accesses to n,x, which is the same instruction
/// without the page-2 prefix, wherever H:X is free to hold the frame base.
FunctionPass *createHCS08StackToIndexedPass();

/// Moves spill slots that are never live across a call out of the frame and
/// into a direct-page bank shared by the whole program, which is a byte
/// shorter to reach and does not need the index register.
FunctionPass *createHCS08DirectPageBankPass();

void initializeHCS08DAGToDAGISelLegacyPass(PassRegistry &);
void initializeHCS08AsmPrinterPass(PassRegistry &);
void initializeHCS08FuseCompareBranchPass(PassRegistry &);
void initializeHCS08StackToIndexedPass(PassRegistry &);
void initializeHCS08DirectPageBankPass(PassRegistry &);

} // namespace llvm

#endif // LLVM_LIB_TARGET_HCS08_HCS08_H
