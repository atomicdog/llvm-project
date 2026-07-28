//===- HCS08.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

//===----------------------------------------------------------------------===//
// HCS08 ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

/// The ABI itself is the default one - this exists only to carry the target's
/// function attributes across, which the default TargetCodeGenInfo has no hook
/// for. Keep it on DefaultABIInfo: the calling convention is described in the
/// backend, and changing the front end's view of it here would silently move
/// every argument.
class HCS08TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  HCS08TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<DefaultABIInfo>(CGT)) {}

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &M) const override {
    if (GV->isDeclaration())
      return;
    const auto *FD = dyn_cast_or_null<FunctionDecl>(D);
    if (!FD)
      return;
    auto *Fn = cast<llvm::Function>(GV);

    // Read by HCS08FrameLowering, which turns it into the pshh/pulh pair the
    // hardware does not provide and an rti in place of the rts.
    if (FD->getAttr<HCS08InterruptAttr>())
      Fn->addFnAttr("hcs08-interrupt");

    // A promise that neither this function nor anything it calls touches the
    // direct-page spill bank, so a handler need not preserve it. The half that
    // is checkable is enforced by getHCS08DPBankSize reporting zero for such a
    // function, which keeps the compiler from handing it a slot to contradict
    // the promise with.
    if (FD->getAttr<HCS08NoDirectPageBankAttr>())
      Fn->addFnAttr("hcs08-no-dp-bank");
  }
};

} // namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createHCS08TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<HCS08TargetCodeGenInfo>(CGM.getTypes());
}
