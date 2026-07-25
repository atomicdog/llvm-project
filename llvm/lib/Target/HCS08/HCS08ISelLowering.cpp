//===-- HCS08ISelLowering.cpp - HCS08 DAG Lowering Implementation ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HCS08ISelLowering.h"
#include "HCS08.h"
#include "HCS08SelectionDAGInfo.h"
#include "HCS08Subtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "hcs08-lower"

#define GET_CALLING_CONV_IMPL
#include "HCS08GenCallingConv.inc"

HCS08TargetLowering::HCS08TargetLowering(const TargetMachine &TM,
                                         const HCS08Subtarget &STI)
    : TargetLowering(TM, STI) {
  // Set up the register classes.
  addRegisterClass(MVT::i8, &HCS08::GR8RegClass);
  addRegisterClass(MVT::i16, &HCS08::GR16RegClass);

  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(HCS08::SP);
  setBooleanContents(ZeroOrOneBooleanContent);

  // A global address is materialized as a wrapped target address.
  setOperationAction(ISD::GlobalAddress, MVT::i16, Custom);

  // Conditional branches go through a compare that sets the condition codes.
  setOperationAction(ISD::BR_CC, MVT::i8, Custom);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
}

SDValue HCS08TargetLowering::LowerOperation(SDValue Op,
                                            SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  default:
    llvm_unreachable("unimplemented operation lowering");
  }
}

// Translate an ISD condition code for an integer compare of A against an
// operand into the HCS08 branch condition. A is the left-hand side of the
// compare, so no operand swap is needed.
static unsigned translateCC(ISD::CondCode CC) {
  switch (CC) {
  case ISD::SETEQ:  return HCS08CC::COND_EQ;
  case ISD::SETNE:  return HCS08CC::COND_NE;
  case ISD::SETUGE: return HCS08CC::COND_HS;
  case ISD::SETULT: return HCS08CC::COND_LO;
  case ISD::SETUGT: return HCS08CC::COND_HI;
  case ISD::SETULE: return HCS08CC::COND_LS;
  case ISD::SETGE:  return HCS08CC::COND_GE;
  case ISD::SETLT:  return HCS08CC::COND_LT;
  case ISD::SETGT:  return HCS08CC::COND_GT;
  case ISD::SETLE:  return HCS08CC::COND_LE;
  default:
    report_fatal_error("unsupported integer condition code");
  }
}

SDValue HCS08TargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc dl(Op);

  SDValue Flag = DAG.getNode(HCS08ISD::CMP, dl, MVT::Glue, LHS, RHS);
  SDValue TargetCC = DAG.getConstant(translateCC(CC), dl, MVT::i8);
  return DAG.getNode(HCS08ISD::BR_CC, dl, MVT::Other, Chain, Dest, TargetCC,
                     Flag);
}

SDValue HCS08TargetLowering::LowerGlobalAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  auto *GA = cast<GlobalAddressSDNode>(Op);
  SDLoc dl(Op);
  SDValue Result = DAG.getTargetGlobalAddress(GA->getGlobal(), dl, MVT::i16,
                                              GA->getOffset());
  return DAG.getNode(HCS08ISD::Wrapper, dl, MVT::i16, Result);
}

SDValue HCS08TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  // Phase 0 supports only functions with no incoming arguments.
  if (!Ins.empty())
    report_fatal_error("HCS08 argument lowering not yet implemented");
  return Chain;
}

SDValue HCS08TargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                       SmallVectorImpl<SDValue> &InVals) const {
  report_fatal_error("HCS08 call lowering not yet implemented");
}

bool HCS08TargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_HCS08);
}

SDValue HCS08TargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &dl,
    SelectionDAG &DAG) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_HCS08);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");
    Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), OutVals[i], Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);

  return DAG.getNode(HCS08ISD::RET_GLUE, dl, MVT::Other, RetOps);
}
