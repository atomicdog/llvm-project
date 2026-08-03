//===-- HCS08ISelLowering.h - HCS08 DAG Lowering Interface -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Interfaces that HCS08 uses to lower LLVM code into a selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HCS08_HCS08ISELLOWERING_H
#define LLVM_LIB_TARGET_HCS08_HCS08ISELLOWERING_H

#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class HCS08Subtarget;

class HCS08TargetLowering : public TargetLowering {
public:
  explicit HCS08TargetLowering(const TargetMachine &TM,
                               const HCS08Subtarget &STI);

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;

  /// A comparison result is a byte: it is produced by selecting between 1 and
  /// 0, and the accumulator is where that lands.
  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;

  /// A shift count is a byte: it is counted down in the accumulator's width,
  /// not the pointer's, which is what it would default to.
  MVT getScalarShiftAmountTy(const DataLayout &DL, EVT LHSTy) const override {
    return MVT::i8;
  }

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *MBB) const override;

  /// Inline assembly operand constraints. 'a' is the accumulator and 'x' is
  /// the H:X pair; clang already accepts both, but without these hooks the
  /// allocator has nothing to satisfy them with and every asm with a register
  /// operand fails with "could not allocate output register for constraint".
  ConstraintType getConstraintType(StringRef Constraint) const override;

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;

private:
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerShift(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerCallResult(SDValue Chain, SDValue InGlue,
                          CallingConv::ID CallConv, bool isVarArg,
                          const SmallVectorImpl<ISD::InputArg> &Ins,
                          const SDLoc &dl, SelectionDAG &DAG,
                          SmallVectorImpl<SDValue> &InVals) const;

  SDValue
  LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                       const SmallVectorImpl<ISD::InputArg> &Ins,
                       const SDLoc &dl, SelectionDAG &DAG,
                       SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context, const Type *RetTy) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &dl,
                      SelectionDAG &DAG) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HCS08_HCS08ISELLOWERING_H
