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

    // double is 32 bits here - the same type as float - and so is long double.
    //
    // 64-bit double is not a configuration this target has: __divdf3 needs a
    // 302-byte frame against a 256-byte ceiling and will not compile at any
    // setting, and even if it did, muldf3 and divdf3 are 14KB and 19KB against
    // a part with 32KB of flash. Leaving double at 64 bits therefore does not
    // buy conformance, it buys a link error against __adddf3 the first time
    // anyone writes a floating-point constant without an f suffix - and,
    // because C promotes a float through "..." to double, it also made
    // printf("%f") impossible.
    //
    // This is what SDCC does for these parts, and what avr-gcc spells
    // -fshort-double; AVR's clang target does the same unconditionally. The
    // -mdouble= flag stays AVR-only in the driver, so asking for 64 here is a
    // clean "unsupported option for target" rather than a distant undefined
    // symbol.
    DoubleWidth = LongDoubleWidth = 32;
    DoubleFormat = LongDoubleFormat = &llvm::APFloat::IEEEsingle();

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

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override;

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
