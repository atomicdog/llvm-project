//===-- ABIHCS08.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ABI_HCS08_ABIHCS08_H
#define LLDB_SOURCE_PLUGINS_ABI_HCS08_ABIHCS08_H

#include "lldb/Target/ABI.h"
#include "lldb/lldb-private.h"

/// The calling convention for the NXP (Freescale) HCS08.
///
/// There is no published psABI for this family, so this is not "SysV
/// anything" - the convention, the DWARF register numbers and the relocation
/// set are all LLVM-defined here, and are documented in
/// llvm/lib/Target/HCS08/CodeGenDesign.md. The plugin is named after the
/// machine rather than after a standard it does not implement.
class ABIHCS08 : public lldb_private::RegInfoBasedABI {
public:
  ~ABIHCS08() override = default;

  size_t GetRedZoneSize() const override;

  bool PrepareTrivialCall(lldb_private::Thread &thread, lldb::addr_t sp,
                          lldb::addr_t functionAddress,
                          lldb::addr_t returnAddress,
                          llvm::ArrayRef<lldb::addr_t> args) const override;

  bool GetArgumentValues(lldb_private::Thread &thread,
                         lldb_private::ValueList &values) const override;

  lldb_private::Status
  SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                       lldb::ValueObjectSP &new_value) override;

  lldb::ValueObjectSP
  GetReturnValueObjectImpl(lldb_private::Thread &thread,
                           lldb_private::CompilerType &type) const override;

  lldb::UnwindPlanSP CreateFunctionEntryUnwindPlan() override;

  lldb::UnwindPlanSP CreateDefaultUnwindPlan() override;

  bool RegisterIsVolatile(const lldb_private::RegisterInfo *reg_info) override;

  /// The stack is byte-addressed and has no alignment requirement at all on
  /// this machine, so - unlike the MSP430 plugin this one is modelled on -
  /// there is no even-address check to make. All that can be said is that the
  /// CFA is a real 16-bit address.
  bool CallFrameAddressIsValid(lldb::addr_t cfa) override {
    return cfa != 0 && cfa <= UINT16_MAX;
  }

  bool CodeAddressIsValid(lldb::addr_t pc) override { return pc <= UINT16_MAX; }

  const lldb_private::RegisterInfo *
  GetRegisterInfoArray(uint32_t &count) override;

  /// Only ever consulted when JITting an expression, which this target cannot
  /// do. 256 is the frame size ceiling the backend imposes (`n,sp` reaches 256
  /// bytes), and is the largest answer that could ever be honest here.
  uint64_t GetStackFrameSize() override { return 256; }

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------

  static void Initialize();

  static void Terminate();

  static lldb::ABISP CreateInstance(lldb::ProcessSP process_sp,
                                    const lldb_private::ArchSpec &arch);

  static llvm::StringRef GetPluginNameStatic() { return "hcs08"; }

  // PluginInterface protocol

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

private:
  using lldb_private::RegInfoBasedABI::RegInfoBasedABI;
};

#endif // LLDB_SOURCE_PLUGINS_ABI_HCS08_ABIHCS08_H
