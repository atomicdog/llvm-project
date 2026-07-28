//===------ SemaHCS08.cpp -------- HCS08 target-specific routines ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to HCS08.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaHCS08.h"
#include "clang/AST/DeclBase.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Sema/Attr.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"

namespace clang {
SemaHCS08::SemaHCS08(Sema &S) : SemaBase(S) {}

void SemaHCS08::handleInterruptAttr(Decl *D, const ParsedAttr &AL) {
  if (!isFuncOrMethodForAttrSubject(D)) {
    Diag(D->getLocation(), diag::warn_attribute_wrong_decl_type)
        << AL << AL.isRegularKeywordAttribute() << ExpectedFunction;
    return;
  }

  // Which vector a handler serves is the linker script's business, so unlike
  // MSP430's the attribute takes no number.
  if (!AL.checkExactlyNumArgs(SemaRef, 0))
    return;

  // Nothing calls a handler, so there is nowhere for arguments to come from or
  // a result to go; the hardware provides the one and discards the other.
  if (hasFunctionProto(D) && getFunctionOrMethodNumParams(D) != 0) {
    Diag(D->getLocation(), diag::warn_interrupt_signal_attribute_invalid)
        << /*HCS08*/ 4 << /*interrupt*/ 0 << 0;
    return;
  }
  if (!getFunctionOrMethodResultType(D)->isVoidType()) {
    Diag(D->getLocation(), diag::warn_interrupt_signal_attribute_invalid)
        << /*HCS08*/ 4 << /*interrupt*/ 0 << 1;
    return;
  }

  handleSimpleAttribute<HCS08InterruptAttr>(*this, D, AL);
}

} // namespace clang
