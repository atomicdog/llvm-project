
//#include "H2BLBSubtarget.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include <memory>
#include <optional>

namespace llvm {

class EpromintTargetMachine : public CodeGenTargetMachineImpl {


public:
  EpromintTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                     StringRef FS, const TargetOptions &Options,
                     std::optional<Reloc::Model> RM,
                     std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                     bool JIT);
  ~EpromintTargetMachine() override;

};

} // end namespace llvm

#endif
