
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

Target &llvm::getTheEpromintTarget() {
  static Target TheEpromintTarget;
  return TheEpromintTarget;
}


extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeEpromintTargetInfo() {
  RegisterTarget<Triple::epromint> X(getTheEpromintTarget(), "epromint",
                "EPROMINT Microcontroller", "EPROMINT");
}