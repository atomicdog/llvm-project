//===-- ABIHCS08.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIHCS08.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/Status.h"
#include "lldb/ValueObject/ValueObject.h"

#include "llvm/TargetParser/Triple.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABIHCS08, ABIHCS08)

/// The DWARF numbering from CodeGenDesign.md section 23, which is also what
/// HCS08RegisterInfo.td hands to the compiler. There is no published psABI for
/// this family, so this numbering is LLVM-defined; these values and the ones in
/// the .td are one fact written down twice, and a debugger built against the
/// wrong copy produces plausible wrong backtraces rather than an error.
///
/// H:X carries a number of its own as well as its halves because it is the only
/// allocatable 16-bit register, so it is where a pointer or an `int` actually
/// lives, and location lists name it constantly.
enum dwarf_regnums {
  dwarf_a = 0,
  dwarf_h = 1,
  dwarf_x = 2,
  dwarf_hx = 3,
  dwarf_sp = 4,
  dwarf_pc = 5,
  dwarf_nzv = 6,
  dwarf_c = 7,
};

/// This table is a name-to-number map and nothing else: RegInfoBasedABI reads
/// it only through GetRegisterInfoByName, on behalf of AugmentRegisterInfo,
/// to fill in the DWARF and generic numbers for whatever registers the remote
/// stub declared in its target XML. Nothing consults byte_offset, so - as in
/// the MSP430 plugin - every entry leaves it 0 rather than implying a register
/// dump layout that this file has no business dictating.
///
/// alt_name is "" rather than nullptr on purpose: GetRegisterInfoByName builds
/// a StringRef from it, and StringRef asserts on a null pointer.
static const RegisterInfo g_register_infos[] = {
    {"a",
     "",
     1,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_a, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr},
    {"h",
     "",
     1,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_h, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr},
    {"x",
     "",
     1,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_x, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr},
    {"hx",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_hx, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr},
    {"sp",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_sp, LLDB_REGNUM_GENERIC_SP,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr},
    {"pc",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_pc, LLDB_REGNUM_GENERIC_PC,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr},
    // The architectural condition code register. The compiler models it as the
    // two halves below because which half an instruction writes is what decides
    // whether a sequence is correct, but the hardware - and so a BDM probe, and
    // so a stub - has one 8-bit CCR. It has no DWARF number because the
    // numbering was defined for the halves; nothing in .debug_frame or a
    // location list ever names it.
    {"ccr",
     "",
     1,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_REGNUM_GENERIC_FLAGS,
      LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr},
    {"nzv",
     "",
     1,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_nzv, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr},
    {"c",
     "",
     1,
     0,
     eEncodingUint,
     eFormatHex,
     {LLDB_INVALID_REGNUM, dwarf_c, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr},
};

static const uint32_t k_num_register_infos = std::size(g_register_infos);

const RegisterInfo *ABIHCS08::GetRegisterInfoArray(uint32_t &count) {
  count = k_num_register_infos;
  return g_register_infos;
}

size_t ABIHCS08::GetRedZoneSize() const { return 0; }

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------

ABISP ABIHCS08::CreateInstance(lldb::ProcessSP process_sp,
                               const ArchSpec &arch) {
  if (arch.GetTriple().getArch() == llvm::Triple::hcs08)
    return ABISP(new ABIHCS08(std::move(process_sp), MakeMCRegisterInfo(arch)));
  return ABISP();
}

// Everything below that returns nothing does so deliberately, following the
// MSP430 plugin. Calling a function from the debugger needs somewhere on the
// target to put a JIT-compiled expression and its stack, and a 32KB part with
// its code in flash has neither. So: no expression evaluation, and no ABI-level
// return-value reads. Backtraces, registers, memory, breakpoints, stepping and
// variable inspection all come off the DWARF instead, which the backend already
// emits and which is verified against silicon.

bool ABIHCS08::PrepareTrivialCall(Thread &thread, lldb::addr_t sp,
                                  lldb::addr_t pc, lldb::addr_t ra,
                                  llvm::ArrayRef<addr_t> args) const {
  return false;
}

bool ABIHCS08::GetArgumentValues(Thread &thread, ValueList &values) const {
  return false;
}

Status ABIHCS08::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                                      lldb::ValueObjectSP &new_value_sp) {
  return Status();
}

ValueObjectSP
ABIHCS08::GetReturnValueObjectImpl(Thread &thread,
                                   CompilerType &return_compiler_type) const {
  return ValueObjectSP();
}

/// Both unwind plans below say the same two things, and the second of them is
/// the one detail this target does not share with any ordinary machine.
///
/// **The CFA is the caller's SP**, which on entry is `SP+2`: `jsr` pushed two
/// bytes and SP points one *below* the last byte pushed. Choosing the caller's
/// SP rather than its lowest occupied byte is deliberate - an unwinder recovers
/// the caller's SP by assigning it the CFA, so any other definition walks every
/// parent frame off by one, silently and cumulatively.
///
/// **The return address is at `CFA-1`, not `CFA-2`.** `jsr` stores PCL at the
/// CFA itself and PCH one below it, so the two bytes read high-first begin one
/// below the CFA. Getting this wrong does not fail; it prints a plausible wrong
/// caller. See CodeGenDesign.md section 24.
static UnwindPlan::Row MakeHCS08EntryRow() {
  UnwindPlan::Row row;
  row.GetCFAValue().SetIsRegisterPlusOffset(dwarf_sp, 2);
  row.SetRegisterLocationToAtCFAPlusOffset(dwarf_pc, -1, true);
  row.SetRegisterLocationToIsCFAPlusOffset(dwarf_sp, 0, true);
  return row;
}

// called when we are on the first instruction of a new function
UnwindPlanSP ABIHCS08::CreateFunctionEntryUnwindPlan() {
  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  plan_sp->AppendRow(MakeHCS08EntryRow());
  plan_sp->SetSourceName("hcs08 at-func-entry default");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  return plan_sp;
}

UnwindPlanSP ABIHCS08::CreateDefaultUnwindPlan() {
  // There is no frame pointer to fall back on - the backend addresses locals
  // as n,sp throughout and never reserves a register for a frame base - so
  // this plan is only right where SP still holds its on-entry value. That is a
  // fallback for hand-written assembly carrying no CFI; everything the compiler
  // emits describes every instruction boundary in .debug_frame and is used in
  // preference to this.
  UnwindPlan::Row row = MakeHCS08EntryRow();
  row.SetUnspecifiedRegistersAreUndefined(true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("hcs08 default unwind plan");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  plan_sp->SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  return plan_sp;
}

bool ABIHCS08::RegisterIsVolatile(const RegisterInfo *reg_info) {
  // Nothing at all is callee-saved here: CSR_HCS08 in the backend is the empty
  // list, so A, H, X and H:X are clobbered by a call, and the condition codes
  // are clobbered by very nearly every instruction. SP is the one register a
  // call gives back, and ABI::GetFallbackRegisterLocation answers for it (the
  // caller's SP is the CFA) before reaching this function.
  //
  // The MSP430 plugin decides this by indexing on `byte_offset / 2`, which
  // cannot be borrowed: that assumes a register file of uniform 2-byte entries,
  // and this one mixes 1-byte (A, H, X, CCR) with 2-byte (H:X, SP, PC).
  return true;
}

void ABIHCS08::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "ABI for HCS08 targets", CreateInstance);
}

void ABIHCS08::Terminate() { PluginManager::UnregisterPlugin(CreateInstance); }
