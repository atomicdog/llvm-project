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
#include "llvm/MC/MCContext.h"
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

  // No jump tables, and saying so is what stops one being built.
  //
  // SelectionDAGBuilder asks areJTsAllowed() before turning a dense switch into
  // a table, and the default answer is yes if either BR_JT or BRIND is legal.
  // Neither was declared here, so both defaulted to Legal, a switch with enough
  // dense cases became a jump table, and instruction selection then had nothing
  // to select it with - "Cannot select: br_jt", a fatal error rather than bad
  // code. The first thing to hit it was a printf's conversion switch.
  //
  // Expand on both is the right answer rather than a workaround. A jump table
  // needs an indirect jump, and this machine has none: the only way to reach a
  // computed address is to push it and rts, which is what the indirect-call
  // path does at four bytes of outgoing stack per call. A comparison chain is
  // cheaper than that for any switch small enough to appear on an 8-bit part,
  // and it is what the target already generates everywhere else.
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::BRIND, MVT::Other, Expand);

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

  // There is no instruction that widens a value already sitting in a register,
  // so widening one is a shift pair. A boolean widened to a mask is the common
  // case and reaches this from ordinary C.
  for (MVT VT : {MVT::i1, MVT::i8})
    setOperationAction(ISD::SIGN_EXTEND_INREG, VT, Expand);

  // None of the bit-counting or bit-permuting operations exist here. Ordinary
  // C reaches them through __builtin_clz and friends, and compiler-rt's own
  // division routines are written in terms of them.
  for (MVT VT : {MVT::i8, MVT::i16}) {
    for (auto Op : {ISD::CTLZ, ISD::CTTZ, ISD::CTPOP, ISD::BSWAP,
                    ISD::BITREVERSE, ISD::ROTL, ISD::ROTR})
      setOperationAction(Op, VT, Expand);
  }

  setTargetDAGCombine(ISD::ADD);

  // The hardware multiply and divide are byte-wide and unsigned, so everything
  // wider, and signed division, belongs to the runtime library - which does
  // not exist yet, making these calls that fail to link rather than a compiler
  // that crashes. A 16-bit variable shift wants the same treatment but cannot
  // have it: LibCall is not an action legalization honours for a shift, so it
  // stays unsupported until there is a library to call.
  for (auto Op : {ISD::MUL, ISD::UDIV, ISD::SDIV, ISD::UREM, ISD::SREM})
    setOperationAction(Op, MVT::i16, LibCall);
  for (auto Op : {ISD::SDIV, ISD::SREM})
    setOperationAction(Op, MVT::i8, LibCall);
  // Nothing produces the high half of a product, and nothing shifts a value
  // wider than a word, so an operation on a wider type has to become a call
  // rather than a chain of narrower ones.
  for (MVT VT : {MVT::i8, MVT::i16}) {
    for (auto Op : {ISD::MULHU, ISD::MULHS, ISD::UMUL_LOHI, ISD::SMUL_LOHI})
      setOperationAction(Op, VT, Expand);
    for (auto Op : {ISD::SHL_PARTS, ISD::SRA_PARTS, ISD::SRL_PARTS})
      setOperationAction(Op, VT, Expand);
  }

  // Varargs. The va_list is a plain pointer, so va_arg is the generic
  // expansion - load the pointer, load the value, store the pointer past it -
  // and va_copy is a pointer copy. Nothing is aligned to more than a byte on
  // this target, so none of that has any padding to step over. Only va_start
  // needs the target to say anything, and what it says is where the unnamed
  // arguments begin.
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Expand);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);

  // The ALU shifts one bit at a time. A byte shifted by a constant is that
  // many single-bit shifts; everything else goes to the runtime, which is
  // where a loop belongs - see LowerShift. i16 is custom rather than LibCall
  // because the sign splat has to be picked out of it first, and because
  // LibCall is not an action legalization honours for a shift anyway.
  for (MVT VT : {MVT::i8, MVT::i16})
    for (auto Op : {ISD::SHL, ISD::SRL, ISD::SRA})
      setOperationAction(Op, VT, Custom);
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
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  default:
    llvm_unreachable("unimplemented operation lowering");
  }
}

/// Get (creating on first use) one of the function's scratch slots.
///
/// The direct-page bank is preferred and a frame object is the fallback. Every
/// use is a byte cheaper there, and a function whose frame held nothing else
/// loses its prologue and epilogue with it. The bank is safe for these without
/// any of the analysis HCS08DirectPageBank has to do, because each expansion
/// below fills its slot and consumes it within itself: no two uses are ever
/// live at once, and nothing in one outlives a call, since there is no call in
/// any of them.
static const HCS08Scratch &getScratch(MachineFunction &MF, HCS08Scratch &S,
                                      unsigned Bytes) {
  if (!S.isValid()) {
    S.Bank = MF.getInfo<HCS08MachineFunctionInfo>()->allocDPBank(Bytes);
    if (!S.inBank())
      S.FI = MF.getFrameInfo().CreateSpillStackObject(Bytes, Align(1));
  }
  return S;
}

/// The direct-page counterpart of a frame-slot access, if the slot is in the
/// bank; the access itself if it is not.
static unsigned slotOpcode(unsigned SPOpc, const HCS08Scratch &S) {
  if (!S.inBank())
    return SPOpc;
  switch (SPOpc) {
  case HCS08::STHXsp:  return HCS08::STHXdir;
  case HCS08::LDHXsp:  return HCS08::LDHXdir;
  case HCS08::STAsp:   return HCS08::STAdir;
  case HCS08::LDAsp:   return HCS08::LDAdir;
  case HCS08::CMP8sp:  return HCS08::CMP8dir;
  case HCS08::CMP16sp: return HCS08::CMP16dir;
  case HCS08::ADD8sp:  return HCS08::ADD8dir;
  case HCS08::SUB8sp:  return HCS08::SUB8dir;
  case HCS08::AND8sp:  return HCS08::AND8dir;
  case HCS08::ORA8sp:  return HCS08::ORA8dir;
  case HCS08::EOR8sp:  return HCS08::EOR8dir;
  }
  llvm_unreachable("no direct-page form of this frame access");
}

/// Append the address of one byte of a scratch slot to a real instruction.
static void addSlotAddr(const MachineInstrBuilder &MIB, const HCS08Scratch &S,
                        int64_t Byte) {
  if (!S.inBank()) {
    MIB.addFrameIndex(S.FI).addImm(Byte);
    return;
  }
  MachineOperand MO = MachineOperand::CreateES(HCS08DPBankSymbol);
  MO.setOffset(S.Bank + Byte);
  MIB.add(MO);
}

/// Append a scratch slot as the (base, displacement) pair of a slotmem operand.
/// The 16-bit chains are expanded long after a frame index would have said
/// where the slot is, so the base names DPB when it is in the bank and carries
/// the frame index when it is not.
static void addSlotOperand(const MachineInstrBuilder &MIB,
                           const HCS08Scratch &S) {
  if (S.inBank())
    MIB.addReg(HCS08::DPB).addImm(S.Bank);
  else
    MIB.addFrameIndex(S.FI).addImm(0);
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

  auto *FuncInfo = MF.getInfo<HCS08MachineFunctionInfo>();
  const HCS08Scratch &S = getScratch(MF, FuncInfo->getWord16Temp(), 2);

  Register Base = MI.getOperand(1).getReg();
  bool BaseIsKill = MI.getOperand(1).isKill();
  int64_t Disp = MI.getOperand(2).getImm();
  addSlotAddr(BuildMI(*MBB, MI, DL, TII.get(slotOpcode(HCS08::STHXsp, S)))
                  .add(MI.getOperand(0)), // the value; dead here, freeing H:X
              S, 0);

  for (unsigned Byte = 0; Byte != 2; ++Byte) {
    Register Tmp = MRI.createVirtualRegister(&HCS08::GR8RegClass);
    unsigned LdOpc = slotOpcode(HCS08::LDAsp, S);
    addSlotAddr(BuildMI(*MBB, MI, DL, TII.get(LdOpc), Tmp), S, Byte);
    // The pointer stays live until the last byte is written - but only dies
    // there if it was dying here at all. A 32-bit store is two of these
    // through the same pointer, and killing it on the first one leaves the
    // second using a register that liveness has been told is dead.
    // Displacement zero has a one-byte form of its own.
    int64_t At = Disp + Byte;
    auto Store = BuildMI(*MBB, MI, DL,
                         TII.get(At == 0 ? HCS08::STAix : HCS08::STAix1))
                     .addReg(Tmp, RegState::Kill)
                     .addReg(Base, getKillRegState(BaseIsKill && Byte == 1));
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
  auto *FuncInfo = MF.getInfo<HCS08MachineFunctionInfo>();
  const HCS08Scratch &A = getScratch(MF, FuncInfo->getWord16Temp(), 2);
  addSlotAddr(BuildMI(*MBB, MI, DL, TII.get(slotOpcode(HCS08::STHXsp, A)))
                  .add(MI.getOperand(1)),
              A, 0);

  const HCS08Scratch *B = nullptr;
  if (!SecondInMemory) {
    B = &getScratch(MF, FuncInfo->getWord16Temp2(), 2);
    addSlotAddr(BuildMI(*MBB, MI, DL, TII.get(slotOpcode(HCS08::STHXsp, *B)))
                    .add(MI.getOperand(2)),
                *B, 0);
  }

  auto Chain = BuildMI(*MBB, MI, DL, TII.get(MemOpc))
                   .add(MI.getOperand(0)); // destination, H:X
  addSlotOperand(Chain, A);
  if (SecondInMemory)
    Chain.add(MI.getOperand(2)).add(MI.getOperand(3));
  else
    addSlotOperand(Chain, *B);

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

  auto *FuncInfo = MF.getInfo<HCS08MachineFunctionInfo>();
  const HCS08Scratch &S =
      Is16 ? getScratch(MF, FuncInfo->getWord16Temp2(), 2)
           : getScratch(MF, FuncInfo->getALUTemp(), 1);

  unsigned ParkOpc = Is16 ? HCS08::STHXsp : HCS08::STAsp;
  unsigned CmpOpc = Is16 ? HCS08::CMP16sp : HCS08::CMP8sp;
  addSlotAddr(BuildMI(*MBB, MI, DL, TII.get(slotOpcode(ParkOpc, S)))
                  .add(MI.getOperand(1)),
              S, 0);
  addSlotAddr(BuildMI(*MBB, MI, DL, TII.get(slotOpcode(CmpOpc, S)))
                  .add(MI.getOperand(0)),
              S, 0);

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

  // A select is a value like any other, so it can be an argument of a call and
  // is then expanded between the frame setup and the call itself. Splitting
  // the block leaves the frame opened in one block and closed in another, and
  // every block in between has to say how much of it is open or the machine
  // verifier rejects the function - a frame size on entry that no predecessor
  // computed. The blocks made here are inside whatever the block they came
  // from was inside.
  unsigned CallFrameSize = TII.getCallFrameSizeAt(MI);
  FalseMBB->setCallFrameSize(CallFrameSize);
  SinkMBB->setCallFrameSize(CallFrameSize);

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

/// Get (creating on first use) the frame-only scratch byte and word.
///
/// These two are not bank candidates. The loop below shifts its word in place
/// with lsl and ror and counts down with tst and dec, the divide reads its byte
/// with ldx, and the direct-page column has none of those - a bank slot could
/// be written but not worked on.
static int getByteTemp(MachineFunction &MF) {
  auto *FuncInfo = MF.getInfo<HCS08MachineFunctionInfo>();
  int FI = FuncInfo->getByteTempFI();
  if (FI == -1) {
    FI = MF.getFrameInfo().CreateSpillStackObject(1, Align(1));
    FuncInfo->setByteTempFI(FI);
  }
  return FI;
}

// Multiply or divide by a value in a register. Both instructions want their
// second operand in X, which ldx reaches from memory, so the operand is parked
// exactly as the reg-reg ALU parks one.
static MachineBasicBlock *emitMulDiv(MachineInstr &MI, MachineBasicBlock *MBB,
                                     unsigned Opc, bool WantRemainder) {
  MachineFunction &MF = *MBB->getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  int FI = getByteTemp(MF);
  auto Def = RegState::Define | RegState::Implicit;

  BuildMI(*MBB, MI, DL, TII.get(HCS08::STAsp))
      .add(MI.getOperand(2))
      .addFrameIndex(FI)
      .addImm(0);
  // div reads H:A, so the high half has to be zero for a byte divide. mul
  // ignores H entirely, and clearing it there would be wasted.
  if (Opc == HCS08::DIV8)
    BuildMI(*MBB, MI, DL, TII.get(HCS08::CLRHd));
  BuildMI(*MBB, MI, DL, TII.get(HCS08::LDXsp)).addFrameIndex(FI).addImm(0);

  if (!WantRemainder) {
    BuildMI(*MBB, MI, DL, TII.get(Opc))
        .add(MI.getOperand(0))
        .add(MI.getOperand(1));
  } else {
    // The remainder comes back in H, and the stack is the only way out of it.
    Register Quot = MF.getRegInfo().createVirtualRegister(&HCS08::GR8RegClass);
    BuildMI(*MBB, MI, DL, TII.get(Opc), Quot).add(MI.getOperand(1));
    BuildMI(*MBB, MI, DL, TII.get(HCS08::PSHH))
        .addReg(HCS08::H, RegState::Implicit);
    BuildMI(*MBB, MI, DL, TII.get(HCS08::PULA))
        .addReg(MI.getOperand(0).getReg(), Def);
  }

  MI.eraseFromParent();
  return MBB;
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
  case HCS08::MUL8rr:
    return emitMulDiv(MI, MBB, HCS08::MUL8, /*WantRemainder=*/false);
  case HCS08::UDIV8rr:
    return emitMulDiv(MI, MBB, HCS08::DIV8, /*WantRemainder=*/false);
  case HCS08::UREM8rr:
    return emitMulDiv(MI, MBB, HCS08::DIV8, /*WantRemainder=*/true);
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
  const HCS08Scratch &S = getScratch(MF, FuncInfo->getALUTemp(), 1);

  addSlotAddr(BuildMI(*MBB, MI, DL, TII.get(slotOpcode(HCS08::STAsp, S)))
                  .add(MI.getOperand(2)), // second source
              S, 0);
  addSlotAddr(BuildMI(*MBB, MI, DL, TII.get(slotOpcode(SpOpc, S)))
                  .add(MI.getOperand(0))  // destination
                  .add(MI.getOperand(1)), // first source, tied to it
              S, 0);

  MI.eraseFromParent();
  return MBB;
}

/// The runtime routine for a 16-bit shift. There is no 8-bit set: an 8-bit
/// shift by a variable amount is widened into one of these rather than given
/// three more routines of its own.
static RTLIB::Libcall shiftLibcall(unsigned Opcode) {
  switch (Opcode) {
  case ISD::SHL: return RTLIB::SHL_I16;
  case ISD::SRL: return RTLIB::SRL_I16;
  case ISD::SRA: return RTLIB::SRA_I16;
  default:
    llvm_unreachable("not a shift");
  }
}

SDValue HCS08TargetLowering::LowerShift(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  SDValue Val = Op.getOperand(0);
  SDValue Cnt = Op.getOperand(1);
  auto *C = dyn_cast<ConstantSDNode>(Cnt);

  // The accumulator shifts one bit at a time, so a byte shifted by a constant
  // is that many single-bit shifts in a row.
  if (VT == MVT::i8 && C) {
    unsigned N = C->getZExtValue() & 0x7;
    unsigned NodeTy = Op.getOpcode() == ISD::SHL   ? HCS08ISD::SHL1
                      : Op.getOpcode() == ISD::SRL ? HCS08ISD::SRL1
                                                   : HCS08ISD::SRA1;
    for (unsigned i = 0; i != N; ++i)
      Val = DAG.getNode(NodeTy, dl, MVT::i8, Val);
    return Val;
  }

  // Widening a word to a doubleword arrives here as an arithmetic shift right
  // by 15. That is a sign splat, and SIGNMASK16 does it in a straight line, so
  // leave the node alone for the pattern to match rather than calling out for
  // it. Returning a null SDValue would not do that: legalization reads that as
  // "no custom lowering after all" and falls through to a generic expansion.
  if (VT == MVT::i16 && Op.getOpcode() == ISD::SRA && C &&
      C->getZExtValue() == 15)
    return Op;

  // Everything else is the library's. Shifting by a variable amount is a loop
  // whichever way it is done, and a loop built here would be a loop inside
  // whatever it was part of: expanded as the argument of a call, its branches
  // land between ADJCALLSTACKDOWN and the call and split the frame across
  // basic blocks. A call has no such problem - the 32-bit shifts and the
  // divides have gone this way from the start - and it is smaller besides,
  // since the loop is written down once instead of at every use.
  //
  // A byte is widened to a word first. Three more routines to save one
  // iteration is not a trade worth making, and the count is already in the
  // right place either way.
  bool IsByte = VT == MVT::i8;
  if (IsByte) {
    unsigned Ext = Op.getOpcode() == ISD::SRA ? ISD::SIGN_EXTEND
                                              : ISD::ZERO_EXTEND;
    Val = DAG.getNode(Ext, dl, MVT::i16, Val);
  }

  MakeLibCallOptions CallOptions;
  CallOptions.setIsSigned(Op.getOpcode() == ISD::SRA);
  SDValue R = makeLibCall(DAG, shiftLibcall(Op.getOpcode()), MVT::i16,
                          {Val, Cnt}, CallOptions, dl)
                  .first;
  return IsByte ? DAG.getNode(ISD::TRUNCATE, dl, MVT::i8, R) : R;
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
  // One result and no outgoing glue, which is what SDT_HCS08SelectCC declares
  // and what the pattern matches. Asking for a {value, glue} pair built a node
  // the DAG verifier rejects - and nothing consumed the glue: the incoming
  // glue from the compare is what holds the two together.
  return DAG.getNode(HCS08ISD::SELECT_CC, dl, Op.getValueType(), TrueV, FalseV,
                     TargetCC, Flag);
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
  MachineFunction &MF = DAG.getMachineFunction();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, isVarArg ? CC_HCS08_VarArg : CC_HCS08);

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

  // The unnamed arguments begin at the first byte past the named ones, and
  // since a variadic function passes everything on the stack that is simply
  // where the incoming area has got to. va_start writes the address of this
  // object; nothing ever reads it as a value, so a one-byte object is enough
  // to name the place.
  if (isVarArg) {
    auto *FuncInfo = MF.getInfo<HCS08MachineFunctionInfo>();
    FuncInfo->setVarArgsFrameIndex(MFI.CreateFixedObject(
        1, CCInfo.getStackSize() + 2, /*IsImmutable=*/true));
  }
  return Chain;
}

// va_start: put the address of the first unnamed argument in the va_list,
// which on this target is a plain pointer and nothing more.
SDValue HCS08TargetLowering::LowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  auto *FuncInfo = MF.getInfo<HCS08MachineFunctionInfo>();
  SDLoc dl(Op);

  SDValue Addr =
      DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), MVT::i16);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), dl, Addr, Op.getOperand(1),
                      MachinePointerInfo(SV));
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

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, isVarArg ? CC_HCS08_VarArg : CC_HCS08);

  // A call to anything but a symbol cannot be a jsr: jsr ,x wants the target
  // in H:X, which is also where the first 16-bit argument goes, and the
  // argument cannot be put anywhere else because the callee is what decides
  // where to read it. The jump is therefore an rts, which takes its
  // destination off the stack and needs no register.
  //
  // That costs four bytes at the bottom of the outgoing arguments:
  //
  //   sp+1, sp+2   the target, which rts pops into PC
  //   sp+3, sp+4   the address to come back to, left on top for the callee
  //   sp+5 ...     the stack arguments, shifted up out of the way
  //
  // which is the same picture a jsr leaves behind, so the callee cannot tell.
  // Both stores happen below, before any argument register is written, and SP
  // does not move at any point - between them that is what keeps the target
  // and the first argument from wanting H:X at the same moment.
  bool IsIndirect =
      !isa<GlobalAddressSDNode>(Callee) && !isa<ExternalSymbolSDNode>(Callee);
  unsigned IndirectBytes = IsIndirect ? HCS08IndirectCallBlockSize : 0;
  unsigned NumBytes = CCInfo.getStackSize() + IndirectBytes;

  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);

  // The label the callee returns to. The asm printer emits it straight after
  // the rts, and CALLind carries it there.
  MCSymbol *RetSym = nullptr;
  if (IsIndirect)
    RetSym = DAG.getMachineFunction().getContext().createTempSymbol();

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
    unsigned Off = VA.getLocMemOffset() + IndirectBytes;
    SDValue Addr = DAG.getNode(HCS08ISD::OutArgAddr, dl, MVT::i16,
                               DAG.getTargetConstant(Off + 1, dl, MVT::i8));
    MemOpChains.push_back(DAG.getStore(
        Chain, dl, Arg, Addr,
        MachinePointerInfo::getStack(DAG.getMachineFunction(), Off)));
  }

  // The two words the rts reads, written here so that they are in place before
  // the argument registers are, which is the whole point of doing it this way.
  if (IsIndirect) {
    auto StoreWord = [&](SDValue V, unsigned Off) {
      SDValue Addr = DAG.getNode(HCS08ISD::OutArgAddr, dl, MVT::i16,
                                 DAG.getTargetConstant(Off + 1, dl, MVT::i8));
      MemOpChains.push_back(DAG.getStore(
          Chain, dl, V, Addr,
          MachinePointerInfo::getStack(DAG.getMachineFunction(), Off)));
    };
    StoreWord(Callee, 0);
    StoreWord(DAG.getNode(HCS08ISD::RetAddr, dl, MVT::i16,
                          DAG.getMCSymbol(RetSym, MVT::i16)),
              2);
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
  else
    // The target is on the stack now; what the call still has to carry is the
    // label to come back to.
    Callee = DAG.getMCSymbol(RetSym, MVT::i16);

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

  // A handler is entered by the interrupt sequence rather than by a jsr, so it
  // has a different frame to undo on the way out. See section 20.
  bool IsInterrupt =
      DAG.getMachineFunction().getFunction().hasFnAttribute("hcs08-interrupt");
  return DAG.getNode(IsInterrupt ? HCS08ISD::RETI_GLUE : HCS08ISD::RET_GLUE, dl,
                     MVT::Other, RetOps);
}
