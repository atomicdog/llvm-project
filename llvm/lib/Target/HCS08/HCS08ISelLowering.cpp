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
#include "llvm/CodeGen/MachineFrameInfo.h"
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

  // The 8-bit ALU shifts one bit at a time; custom-lower shifts by a constant.
  setOperationAction(ISD::SHL, MVT::i8, Custom);
  setOperationAction(ISD::SRL, MVT::i8, Custom);
  setOperationAction(ISD::SRA, MVT::i8, Custom);
}

SDValue HCS08TargetLowering::LowerOperation(SDValue Op,
                                            SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA:
    return LowerShift(Op, DAG);
  default:
    llvm_unreachable("unimplemented operation lowering");
  }
}

SDValue HCS08TargetLowering::LowerShift(SDValue Op, SelectionDAG &DAG) const {
  auto *C = dyn_cast<ConstantSDNode>(Op.getOperand(1));
  if (!C)
    report_fatal_error("HCS08 variable shifts not yet implemented");

  unsigned Cnt = C->getZExtValue() & 0x7; // in-range constant shift amount
  SDLoc dl(Op);
  SDValue Val = Op.getOperand(0);

  unsigned NodeTy;
  switch (Op.getOpcode()) {
  case ISD::SHL: NodeTy = HCS08ISD::SHL1; break;
  case ISD::SRL: NodeTy = HCS08ISD::SRL1; break;
  case ISD::SRA: NodeTy = HCS08ISD::SRA1; break;
  default:
    llvm_unreachable("not a shift");
  }

  for (unsigned i = 0; i != Cnt; ++i)
    Val = DAG.getNode(NodeTy, dl, MVT::i8, Val);
  return Val;
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
  if (isVarArg)
    report_fatal_error("HCS08 varargs not yet implemented");

  MachineFunction &MF = DAG.getMachineFunction();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_HCS08);

  for (CCValAssign &VA : ArgLocs) {
    MVT LocVT = VA.getLocVT();
    SDValue Val;

    if (VA.isRegLoc()) {
      const TargetRegisterClass *RC =
          LocVT == MVT::i16 ? &HCS08::GR16RegClass : &HCS08::GR8RegClass;
      Register VReg = RegInfo.createVirtualRegister(RC);
      RegInfo.addLiveIn(VA.getLocReg(), VReg);
      Val = DAG.getCopyFromReg(Chain, dl, VReg, LocVT);
    } else {
      // A stack argument lives in the caller's frame, above the two-byte
      // return address that jsr pushed. Frame-object offsets are measured
      // from the entry SP (object N sits at entry-SP + 1 + N), so the
      // argument at call-frame offset N is the fixed object at N + 2.
      int FI = MFI.CreateFixedObject(LocVT.getStoreSize(),
                                     VA.getLocMemOffset() + 2,
                                     /*IsImmutable=*/true);
      SDValue FIN = DAG.getFrameIndex(FI, MVT::i16);
      Val = DAG.getLoad(LocVT, dl, Chain, FIN,
                        MachinePointerInfo::getFixedStack(MF, FI));
    }

    // The only widening we do is i1 -> i8; truncate it back.
    if (VA.getLocInfo() != CCValAssign::Full)
      Val = DAG.getNode(ISD::TRUNCATE, dl, VA.getValVT(), Val);

    InVals.push_back(Val);
  }
  return Chain;
}

SDValue HCS08TargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                       SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &dl = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  CallingConv::ID CallConv = CLI.CallConv;
  bool isVarArg = CLI.IsVarArg;
  CLI.IsTailCall = false;

  if (isVarArg)
    report_fatal_error("HCS08 varargs not yet implemented");

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CC_HCS08);
  unsigned NumBytes = CCInfo.getStackSize();

  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);

  SmallVector<std::pair<unsigned, SDValue>, 4> RegsToPass;
  SmallVector<SDValue, 4> MemOpChains;
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = OutVals[i];
    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    default:
      report_fatal_error("unsupported argument location info");
    }
    if (VA.isRegLoc()) {
      RegsToPass.push_back({VA.getLocReg(), Arg});
      continue;
    }

    // The call frame is reserved by the prologue and sits at the bottom of
    // the frame, so call-frame offset N is "N+1,sp": n,sp addresses SP+n, and
    // the lowest byte of the frame is one above SP (see eliminateFrameIndex).
    // These stores carry no frame index, so nothing else applies that bias.
    //
    // Building the address as a target node keeps SP out of the allocator's
    // way - it is a reserved register, and H:X is too scarce to spend on a
    // frame base.
    SDValue Addr =
        DAG.getNode(HCS08ISD::OutArgAddr, dl, MVT::i16,
                    DAG.getTargetConstant(VA.getLocMemOffset() + 1, dl, MVT::i8));
    MemOpChains.push_back(DAG.getStore(
        Chain, dl, Arg, Addr,
        MachinePointerInfo::getStack(DAG.getMachineFunction(),
                                     VA.getLocMemOffset())));
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  SDValue InGlue;
  for (auto &RP : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, dl, RP.first, RP.second, InGlue);
    InGlue = Chain.getValue(1);
  }

  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), dl, MVT::i16);
  else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i16);

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);
  for (auto &RP : RegsToPass)
    Ops.push_back(DAG.getRegister(RP.first, RP.second.getValueType()));
  if (InGlue.getNode())
    Ops.push_back(InGlue);

  Chain = DAG.getNode(HCS08ISD::CALL, dl, NodeTys, Ops);
  InGlue = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, InGlue, dl);
  InGlue = Chain.getValue(1);

  return LowerCallResult(Chain, InGlue, CallConv, isVarArg, Ins, dl, DAG,
                         InVals);
}

SDValue HCS08TargetLowering::LowerCallResult(
    SDValue Chain, SDValue InGlue, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallResult(Ins, RetCC_HCS08);

  for (CCValAssign &VA : RVLocs) {
    Chain = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), VA.getValVT(), InGlue)
                .getValue(1);
    InGlue = Chain.getValue(2);
    InVals.push_back(Chain.getValue(0));
  }
  return Chain;
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
