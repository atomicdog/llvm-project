//===--- HCS08.h - Declare HCS08 target feature support ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares HCS08 TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_HCS08_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_HCS08_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY HCS08TargetInfo : public TargetInfo {
  static const char *const GCCRegNames[];

public:
  HCS08TargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    TLSSupported = false;

    // A 16-bit int, matching the index register and the pointer width.
    IntWidth = 16;
    LongWidth = 32;
    LongLongWidth = 64;
    PointerWidth = 16;
    FloatWidth = 32;
    DoubleWidth = LongDoubleWidth = 64;

    // Nothing on this machine has an alignment requirement: every load and
    // store is a byte at a time or an index register pair, and neither cares.
    // Aligning would only make objects bigger on a part with a few hundred
    // bytes of RAM.
    ShortAlign = IntAlign = LongAlign = LongLongAlign = 8;
    PointerAlign = 8;
    FloatAlign = DoubleAlign = LongDoubleAlign = 8;
    DefaultAlignForAttributeAligned = 8;
    SuitableAlign = 8;

    SizeType = UnsignedInt;
    PtrDiffType = SignedInt;
    IntPtrType = SignedInt;
    IntMaxType = SignedLongLong;
    WCharType = SignedInt;
    WIntType = SignedInt;
    Char16Type = UnsignedInt;
    SigAtomicType = SignedChar;

    // There is no atomic instruction of any width.
    MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 0;

    resetDataLayout();
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  bool allowsLargerPreferedTypeAlignment() const override { return false; }

  bool hasFeature(StringRef Feature) const override {
    return Feature == "hcs08";
  }

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    return {};
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    switch (*Name) {
    case 'a': // the accumulator
    case 'x': // the index register pair, H:X
      Info.setAllowsRegister();
      return true;
    default:
      return false;
    }
  }

  std::string_view getClobbers() const override { return ""; }
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_HCS08_H
