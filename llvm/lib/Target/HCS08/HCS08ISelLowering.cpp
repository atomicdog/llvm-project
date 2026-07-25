//===-- HCS08ISelLowering.cpp - HCS08 DAG Lowering Implementation ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HCS08ISelLowering.h"
#include "HCS08.h"
#include "HCS08MachineFunctionInfo.h"
#include "HCS08SelectionDAGInfo.h"
#include "HCS08Subtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
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

  // Conditional branches and selects both go through a compare that sets the
  // condition codes; there is no way to get a comparison into a register
  // without branching, so setcc becomes a select of 1 and 0.
  for (MVT VT : {MVT::i8, MVT::i16}) {
    setOperationAction(ISD::BR_CC, VT, Custom);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
    setOperationAction(ISD::SELECT, VT, Expand);
    setOperationAction(ISD::SETCC, VT, Expand);
  }
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);

  // There is no widening load: a narrow value is loaded and then extended.
  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i8, Expand);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i8, Expand);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i8, Expand);
  }
  setTruncStoreAction(MVT::i16, MVT::i8, Expand);

  setTargetDAGCombine(ISD::ADD);

  // The 8-bit ALU shifts one bit at a time; custom-lower shifts by a constant.
  setOperationAction(ISD::SHL, MVT::i8, Custom);
  setOperationAction(ISD::SRL, MVT::i8, Custom);
  setOperationAction(ISD::SRA, MVT::i8, Custom);
}

// Fold a constant displacement into the global address it is added to.
//
// The extended forms address a global through one relocatable operand, which
// can carry an addend, so an access partway into a global should not need any
// arithmetic. It arrives as an addition all the same, because the global is
// wrapped before legalization splits a wide value into halves and asks for the
// second one at +2 - and generic combining cannot see a global through the
// wrapper.
SDValue HCS08TargetLowering::PerformDAGCombine(SDNode *N,
                                               DAGCombinerInfo &DCI) const {
  if (N->getOpcode() != ISD::ADD)
    return SDValue();

  SDValue Wrapped = N->getOperand(0);
  auto *Off = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!Off || Wrapped.getOpcode() != HCS08ISD::Wrapper)
    return SDValue();

  auto *GA = dyn_cast<GlobalAddressSDNode>(Wrapped.getOperand(0));
  if (!GA)
    return SDValue();

  SDLoc dl(N);
  SDValue Sum = DCI.DAG.getTargetGlobalAddress(
      GA->getGlobal(), dl, MVT::i16, GA->getOffset() + Off->getSExtValue());
  return DCI.DAG.getNode(HCS08ISD::Wrapper, dl, MVT::i16, Sum);
}

EVT HCS08TargetLowering::getSetCCResultType(const DataLayout &DL,
                                            LLVMContext &Context,
                                            EVT VT) const {
  if (!VT.isVector())
    return MVT::i8;
  return VT.changeVectorElementTypeToInteger();
}

SDValue HCS08TargetLowering::LowerOperation(SDValue Op,
                                            SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::SELECT_CC:
    return LowerSELECT_CC(Op, DAG);
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA:
    return LowerShift(Op, DAG);
  default:
    llvm_unreachable("unimplemented operation lowering");
  }
}

/// Get (creating on first use) one of the function's two scratch words.
static int getWord16Temp(MachineFunction &MF, bool Second) {
  auto *FuncInfo = MF.getInfo<HCS08MachineFunctionInfo>();
  int FI = Second ? FuncInfo->getWord16Temp2FI() : FuncInfo->getWord16TempFI();
  if (FI == -1) {
    FI = MF.getFrameInfo().CreateSpillStackObject(2, Align(1));
    if (Second)
      FuncInfo->setWord16Temp2FI(FI);
    else
      FuncInfo->setWord16TempFI(FI);
  }
  return FI;
}

// Store a 16-bit value through a pointer, a byte at a time.
//
// sthx has no indexed form, and in any case the value and the pointer both
// want H:X. Park the value in a frame slot, then carry its two bytes through A
// - big-endian, so the high half goes to the lower address. Expanding before
// register allocation means the allocator only ever needs H:X for one of the
// two values at a time.
//
// pshh/pshx would park the value in fewer bytes and without a frame slot, but
// it is not safe here: the pointer has to be brought into H:X between the
// pushes and the pulls, and if the allocator satisfies that with a reload, the
// reload's n,sp displacement is measured against an SP the pushes have moved.
// Frame offsets are only valid while SP holds still.
static MachineBasicBlock *emitStore16Indexed(MachineInstr &MI,
                                             MachineBasicBlock *MBB) {
  MachineFunction &MF = *MBB->getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  int FI = getWord16Temp(MF, /*Second=*/false);

  Register Base = MI.getOperand(1).getReg();
  int64_t Disp = MI.getOperand(2).getImm();
  BuildMI(*MBB, MI, DL, TII.get(HCS08::STHXsp))
      .add(MI.getOperand(0)) // the value; dead after this, freeing H:X
      .addFrameIndex(FI)
      .addImm(0);

  for (unsigned Byte = 0; Byte != 2; ++Byte) {
    Register Tmp = MRI.createVirtualRegister(&HCS08::GR8RegClass);
    BuildMI(*MBB, MI, DL, TII.get(HCS08::LDAsp), Tmp)
        .addFrameIndex(FI)
        .addImm(Byte);
    // The pointer stays live until the last byte is written. Displacement zero
    // has a one-byte form of its own.
    int64_t At = Disp + Byte;
    auto Store = BuildMI(*MBB, MI, DL,
                         TII.get(At == 0 ? HCS08::STAix : HCS08::STAix1))
                     .addReg(Tmp, RegState::Kill)
                     .addReg(Base, getKillRegState(Byte == 1));
    if (At != 0)
      Store.addImm(At);
  }

  MI.eraseFromParent();
  return MBB;
}

// Get a 16-bit ALU operand into memory, which is where every form of the
// operation has to read it from.
//
// This is the half of the problem that has to be solved before register
// allocation: H:X is the only 16-bit register, so it cannot hold both
// operands. The byte chain itself is left to expandPostRAPseudo, which is the
// half that has to be solved after.
static MachineBasicBlock *emitALU16(MachineInstr &MI, MachineBasicBlock *MBB,
                                    unsigned MemOpc, bool SecondInMemory) {
  MachineFunction &MF = *MBB->getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  // The result is built in the first operand's slot, so that one is always a
  // scratch word of ours - the second may be any frame object, since the chain
  // only reads it.
  int FIa = getWord16Temp(MF, /*Second=*/false);
  BuildMI(*MBB, MI, DL, TII.get(HCS08::STHXsp))
      .add(MI.getOperand(1))
      .addFrameIndex(FIa)
      .addImm(0);

  int FIb = -1;
  if (!SecondInMemory) {
    FIb = getWord16Temp(MF, /*Second=*/true);
    BuildMI(*MBB, MI, DL, TII.get(HCS08::STHXsp))
        .add(MI.getOperand(2))
        .addFrameIndex(FIb)
        .addImm(0);
  }

  auto Chain = BuildMI(*MBB, MI, DL, TII.get(MemOpc))
                   .add(MI.getOperand(0)) // destination, H:X
                   .addFrameIndex(FIa)
                   .addImm(0);
  if (SecondInMemory)
    Chain.add(MI.getOperand(2)).add(MI.getOperand(3));
  else
    Chain.addFrameIndex(FIb).addImm(0);

  MI.eraseFromParent();
  return MBB;
}

// Compare against an operand that is in a register, by parking it first.
//
// The comparison itself reads memory, like every other second operand on this
// machine. The park has to come before the compare, which it does: it is
// emitted here, ahead of the compare, and sthx and sta do not outlive it.
static MachineBasicBlock *emitCmpParked(MachineInstr &MI,
                                        MachineBasicBlock *MBB, bool Is16) {
  MachineFunction &MF = *MBB->getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  int FI;
  if (Is16) {
    FI = getWord16Temp(MF, /*Second=*/true);
  } else {
    auto *FuncInfo = MF.getInfo<HCS08MachineFunctionInfo>();
    FI = FuncInfo->getALUTempFI();
    if (FI == -1) {
      FI = MF.getFrameInfo().CreateSpillStackObject(1, Align(1));
      FuncInfo->setALUTempFI(FI);
    }
  }

  BuildMI(*MBB, MI, DL, TII.get(Is16 ? HCS08::STHXsp : HCS08::STAsp))
      .add(MI.getOperand(1))
      .addFrameIndex(FI)
      .addImm(0);
  BuildMI(*MBB, MI, DL, TII.get(Is16 ? HCS08::CMP16sp : HCS08::CMP8sp))
      .add(MI.getOperand(0))
      .addFrameIndex(FI)
      .addImm(0);

  MI.eraseFromParent();
  return MBB;
}

// Expand a select into a diamond: the condition codes are already set, so
// branch over the assignment of the false value.
//
//   thisMBB:  bCC sinkMBB          (true value is live out of here)
//   falseMBB: (empty, falls through)
//   sinkMBB:  dst = PHI [true, thisMBB], [false, falseMBB]
static MachineBasicBlock *emitSelect(MachineInstr &MI,
                                     MachineBasicBlock *ThisMBB) {
  MachineFunction &MF = *ThisMBB->getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  const BasicBlock *BB = ThisMBB->getBasicBlock();
  MachineFunction::iterator It = ++ThisMBB->getIterator();

  MachineBasicBlock *FalseMBB = MF.CreateMachineBasicBlock(BB);
  MachineBasicBlock *SinkMBB = MF.CreateMachineBasicBlock(BB);
  MF.insert(It, FalseMBB);
  MF.insert(It, SinkMBB);

  // Everything after the select moves to the join block.
  SinkMBB->splice(SinkMBB->begin(), ThisMBB,
                  std::next(MachineBasicBlock::iterator(MI)), ThisMBB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(ThisMBB);

  ThisMBB->addSuccessor(FalseMBB);
  ThisMBB->addSuccessor(SinkMBB);
  FalseMBB->addSuccessor(SinkMBB);

  unsigned CC = MI.getOperand(3).getImm();
  BuildMI(ThisMBB, DL, TII.get(HCS08InstrInfo::getCondBranchOpcode(CC)))
      .addMBB(SinkMBB);

  BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(HCS08::PHI),
          MI.getOperand(0).getReg())
      .addReg(MI.getOperand(1).getReg())
      .addMBB(ThisMBB)
      .addReg(MI.getOperand(2).getReg())
      .addMBB(FalseMBB);

  MI.eraseFromParent();
  return SinkMBB;
}

MachineBasicBlock *
HCS08TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                 MachineBasicBlock *MBB) const {
  unsigned SpOpc;
  switch (MI.getOpcode()) {
  case HCS08::STHXix:
    return emitStore16Indexed(MI, MBB);
  case HCS08::CMP8rr:  return emitCmpParked(MI, MBB, /*Is16=*/false);
  case HCS08::CMP16rr: return emitCmpParked(MI, MBB, /*Is16=*/true);
  case HCS08::SELECT8:
  case HCS08::SELECT16:
    return emitSelect(MI, MBB);
  case HCS08::ADD16rr: return emitALU16(MI, MBB, HCS08::ADD16m, false);
  case HCS08::SUB16rr: return emitALU16(MI, MBB, HCS08::SUB16m, false);
  case HCS08::AND16rr: return emitALU16(MI, MBB, HCS08::AND16m, false);
  case HCS08::ORA16rr: return emitALU16(MI, MBB, HCS08::ORA16m, false);
  case HCS08::EOR16rr: return emitALU16(MI, MBB, HCS08::EOR16m, false);
  case HCS08::ADD16rm: return emitALU16(MI, MBB, HCS08::ADD16m, true);
  case HCS08::SUB16rm: return emitALU16(MI, MBB, HCS08::SUB16m, true);
  case HCS08::AND16rm: return emitALU16(MI, MBB, HCS08::AND16m, true);
  case HCS08::ORA16rm: return emitALU16(MI, MBB, HCS08::ORA16m, true);
  case HCS08::EOR16rm: return emitALU16(MI, MBB, HCS08::EOR16m, true);
  case HCS08::ADD8rr: SpOpc = HCS08::ADD8sp; break;
  case HCS08::SUB8rr: SpOpc = HCS08::SUB8sp; break;
  case HCS08::AND8rr: SpOpc = HCS08::AND8sp; break;
  case HCS08::ORA8rr: SpOpc = HCS08::ORA8sp; break;
  case HCS08::EOR8rr: SpOpc = HCS08::EOR8sp; break;
  default:
    llvm_unreachable("unexpected instruction for the custom inserter");
  }

  // HCS08 has no register/register ALU, and A is the only allocatable 8-bit
  // register, so the second source has to be in memory: give it a slot of its
  // own and read it back from there. Doing this before register allocation is
  // what makes it reliable - the allocator then never has to hold two 8-bit
  // values at once, and only has to spill the tied source across the store.
  MachineFunction &MF = *MBB->getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  auto *FuncInfo = MF.getInfo<HCS08MachineFunctionInfo>();
  int FI = FuncInfo->getALUTempFI();
  if (FI == -1) {
    FI = MF.getFrameInfo().CreateSpillStackObject(1, Align(1));
    FuncInfo->setALUTempFI(FI);
  }

  BuildMI(*MBB, MI, DL, TII.get(HCS08::STAsp))
      .add(MI.getOperand(2)) // second source
      .addFrameIndex(FI)
      .addImm(0);
  BuildMI(*MBB, MI, DL, TII.get(SpOpc))
      .add(MI.getOperand(0)) // destination
      .add(MI.getOperand(1)) // first source, tied to the destination
      .addFrameIndex(FI)
      .addImm(0);

  MI.eraseFromParent();
  return MBB;
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

SDValue HCS08TargetLowering::LowerSELECT_CC(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue TrueV = Op.getOperand(2);
  SDValue FalseV = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDLoc dl(Op);

  SDValue Flag = DAG.getNode(HCS08ISD::CMP, dl, MVT::Glue, LHS, RHS);
  SDValue TargetCC = DAG.getConstant(translateCC(CC), dl, MVT::i8);
  SDVTList VTs = DAG.getVTList(Op.getValueType(), MVT::Glue);
  return DAG.getNode(HCS08ISD::SELECT_CC, dl, VTs, TrueV, FalseV, TargetCC,
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
