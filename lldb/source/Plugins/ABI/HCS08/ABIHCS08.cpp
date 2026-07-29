//===-- ABIHCS08.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIHCS08.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include "lldb/ValueObject/ValueObject.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"

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

/// Read H:X out of whatever the stub gave us.
///
/// The index register is the pair of the 8-bit H and X, so a stub may declare
/// it either way: as a composite named "hx", or as the two halves alone, which
/// is what the built-in fallback register set does because those are the
/// registers a BDM probe actually has. Both have to work here, and H is the
/// high half.
static std::optional<uint64_t> ReadHX(RegisterContext &reg_ctx) {
  if (const RegisterInfo *hx = reg_ctx.GetRegisterInfoByName("hx", 0)) {
    RegisterValue value;
    if (reg_ctx.ReadRegister(hx, value))
      return value.GetAsUInt64();
    return std::nullopt;
  }

  const RegisterInfo *h = reg_ctx.GetRegisterInfoByName("h", 0);
  const RegisterInfo *x = reg_ctx.GetRegisterInfoByName("x", 0);
  if (!h || !x)
    return std::nullopt;
  RegisterValue h_value, x_value;
  if (!reg_ctx.ReadRegister(h, h_value) || !reg_ctx.ReadRegister(x, x_value))
    return std::nullopt;
  return ((h_value.GetAsUInt64() & 0xFF) << 8) | (x_value.GetAsUInt64() & 0xFF);
}

/// Read a return value out of a halted frame.
///
/// This needs nothing on the target, which is why it is here when calling a
/// function from the debugger is not: the value is already sitting in a
/// register that the frame has not yet had a chance to clobber.
///
/// **Everything a C function can return in a register comes back in H:X**,
/// including the one-byte types, and that is worth stating because the calling
/// convention appears to say otherwise. `RetCC_HCS08` does assign a bare `i8`
/// to A, but clang never emits a bare `i8` return: a `char` or `_Bool` is
/// returned `signext`/`zeroext`, which the backend widens into H:X. Reading A
/// for a one-byte type would therefore return whatever A happened to hold -
/// a plausible wrong value, which is the failure this target keeps producing.
/// Checked against the compiler rather than assumed: `ret i8 42` compiles to
/// `lda #$2a` and `ret signext i8 42` to `ldhx #$002a`.
///
/// Anything wider than two bytes is returned through a hidden pointer the
/// *caller* supplied on the stack, so once the callee has returned there is
/// nothing left to say where the value went. Those get nothing rather than a
/// guess, as do floats, which are four bytes here and go the same way.
ValueObjectSP
ABIHCS08::GetReturnValueObjectImpl(Thread &thread,
                                   CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  if (!return_compiler_type)
    return return_valobj_sp;

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return return_valobj_sp;

  std::optional<uint64_t> byte_size =
      llvm::expectedToOptional(return_compiler_type.GetByteSize(&thread));
  if (!byte_size || *byte_size == 0 || *byte_size > 2)
    return return_valobj_sp;

  const uint32_t type_flags = return_compiler_type.GetTypeInfo(nullptr);
  if (!(type_flags & (eTypeIsInteger | eTypeIsPointer | eTypeIsEnumeration)))
    return return_valobj_sp;

  std::optional<uint64_t> raw_value = ReadHX(*reg_ctx);
  if (!raw_value)
    return return_valobj_sp;

  // H:X holds the value already extended to 16 bits, so a one-byte type is the
  // low half of it; re-applying the type's own signedness to that byte gives
  // back what the function returned either way.
  Value value;
  value.SetValueType(Value::ValueType::Scalar);
  const bool is_signed = (type_flags & eTypeIsSigned) != 0;
  if (*byte_size == 1) {
    if (is_signed)
      value.GetScalar() = static_cast<int8_t>(*raw_value & 0xFF);
    else
      value.GetScalar() = static_cast<uint8_t>(*raw_value & 0xFF);
  } else {
    if (is_signed)
      value.GetScalar() = static_cast<int16_t>(*raw_value & 0xFFFF);
    else
      value.GetScalar() = static_cast<uint16_t>(*raw_value & 0xFFFF);
  }

  value.SetCompilerType(return_compiler_type);
  return ValueObjectConstResult::Create(thread.GetStackFrameAtIndex(0).get(),
                                        value, ConstString(""));
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

/// Deliberately none.
///
/// LLDB asks for this when the PC is on the very first instruction of a
/// function, and prefers it over the function's own CFI, because unwind info
/// derived from a prologue that has not run yet is not to be trusted. That
/// reasoning does not apply here, and the guess it prefers is wrong for a whole
/// class of functions.
///
/// An architectural answer has to assume every function was entered by `jsr`,
/// which pushed two bytes: CFA = SP+2. An interrupt handler was not. The
/// hardware stacks five bytes before the first instruction of a handler runs,
/// so the CFA is SP+5 there, and nothing about the function's *address* says
/// which kind it is - only its CFI does. Halting on a handler's first
/// instruction is not an exotic case either; it is what setting a breakpoint by
/// the handler's name does.
///
/// Returning nothing makes LLDB fall through to the DWARF, which is right for
/// both kinds. That is safe here in a way it would not be everywhere, because
/// this target's CFI is asynchronous by construction: the frame lowering
/// restates the offset at every instruction that moves SP, precisely so that a
/// BDM probe halting wherever it happens to halt is describable
/// (CodeGenDesign.md §24). A function with no CFI at all is unaffected -- LLDB
/// then falls through again to CreateDefaultUnwindPlan below, whose rule is the
/// CFA = SP+2 this would have returned anyway.
UnwindPlanSP ABIHCS08::CreateFunctionEntryUnwindPlan() { return nullptr; }

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
