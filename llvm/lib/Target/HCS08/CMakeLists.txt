add_llvm_component_group(HCS08)

set(LLVM_TARGET_DEFINITIONS HCS08.td)

tablegen(LLVM HCS08GenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM HCS08GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM HCS08GenCallingConv.inc -gen-callingconv)
tablegen(LLVM HCS08GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM HCS08GenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM HCS08GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM HCS08GenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM HCS08GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM HCS08GenSDNodeInfo.inc -gen-sd-node-info)
tablegen(LLVM HCS08GenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(HCS08CommonTableGen)

add_llvm_target(HCS08CodeGen
  HCS08AsmPrinter.cpp
  HCS08FrameLowering.cpp
  HCS08ISelDAGToDAG.cpp
  HCS08ISelLowering.cpp
  HCS08InstrInfo.cpp
  HCS08MCInstLower.cpp
  HCS08MachineFunctionInfo.cpp
  HCS08RegisterInfo.cpp
  HCS08SelectionDAGInfo.cpp
  HCS08Subtarget.cpp
  HCS08TargetMachine.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  HCS08Desc
  HCS08Info
  SelectionDAG
  Support
  Target
  TargetParser

  ADD_TO_COMPONENT
  HCS08
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
