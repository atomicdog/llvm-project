//===-- HCS08MachineFunctionInfo.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// HCS08-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HCS08_HCS08MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_HCS08_HCS08MACHINEFUNCTIONINFO_H

#include "HCS08.h"
#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

/// Where one of the expansions' scratch temporaries lives. Either a frame
/// object or, when the direct-page bank is switched on and has room, a byte
/// offset into it - the same storage either way, but reached with a two-byte
/// instruction rather than a three-byte one and without a frame at all.
struct HCS08Scratch {
  int FI = -1;
  int Bank = -1;

  bool isValid() const { return FI >= 0 || Bank >= 0; }
  bool inBank() const { return Bank >= 0; }
};

class HCS08MachineFunctionInfo : public MachineFunctionInfo {
  virtual void anchor();

  /// The byte the reg-reg ALU expansion parks its second operand in, unset if
  /// the function has no such expansion. One slot serves the whole function:
  /// each expansion writes it and reads it back in the next instruction, so no
  /// two uses are ever live at once, and a slot per expansion would eat the
  /// 255-byte frame a byte at a time.
  HCS08Scratch ALUTemp;

  /// Two scratch words, unset if unused. The 16-bit expansions park their
  /// operands here: the indexed store uses the first, the 16-bit ALU both.
  /// Shared across the function for the same reason as ALUTemp - each
  /// expansion fills them and consumes them within itself, so no two uses are
  /// ever live at once.
  HCS08Scratch Word16Temp;
  HCS08Scratch Word16Temp2;

  /// The scratch word and byte the variable-shift loop and the multiply and
  /// divide expansions use. These stay in the frame however much bank there
  /// is: they are read by lsl, ror, asr, tst, dec and ldx, and the direct-page
  /// column has none of those, so a bank slot could be written but not worked
  /// on.
  int ByteTempFI = -1;

  /// Bytes of the bank handed out so far. Two things allocate from it - these
  /// temporaries at instruction selection and HCS08DirectPageBank afterwards -
  /// and a slot each is what keeps them from landing on top of one another.
  unsigned DPBankUsed = 0;

public:
  HCS08MachineFunctionInfo() = default;
  HCS08MachineFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  HCS08Scratch &getALUTemp() { return ALUTemp; }
  HCS08Scratch &getWord16Temp() { return Word16Temp; }
  HCS08Scratch &getWord16Temp2() { return Word16Temp2; }


  int getByteTempFI() const { return ByteTempFI; }
  void setByteTempFI(int FI) { ByteTempFI = FI; }

  unsigned getDPBankUsed() const { return DPBankUsed; }

  /// Take `Bytes` from the bank, or return -1 if that would run past the end
  /// of what the linker script was told to reserve.
  int allocDPBank(unsigned Bytes) {
    if (DPBankUsed + Bytes > getHCS08DPBankSize())
      return -1;
    int Offset = DPBankUsed;
    DPBankUsed += Bytes;
    return Offset;
  }

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_HCS08_HCS08MACHINEFUNCTIONINFO_H
