//===--- HCS08.cpp - Implement HCS08 target feature support ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements HCS08 TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "HCS08.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

// The whole programmer's model: an accumulator, an index register addressable
// as a pair or as its two halves, the stack pointer and the condition codes.
const char *const HCS08TargetInfo::GCCRegNames[] = {"a",  "h",  "x",
                                                    "hx", "sp", "ccr"};

ArrayRef<const char *> HCS08TargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

void HCS08TargetInfo::getTargetDefines(const LangOptions &Opts,
                                       MacroBuilder &Builder) const {
  Builder.defineMacro("HCS08");
  Builder.defineMacro("__HCS08__");
  Builder.defineMacro("__hcs08__");
}
