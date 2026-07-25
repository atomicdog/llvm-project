add_llvm_component_library(LLVMHCS08Desc
  HCS08AsmBackend.cpp
  HCS08ELFObjectWriter.cpp
  HCS08InstPrinter.cpp
  HCS08MCAsmInfo.cpp
  HCS08MCCodeEmitter.cpp
  HCS08MCTargetDesc.cpp

  LINK_COMPONENTS
  MC
  HCS08Info
  Support

  ADD_TO_COMPONENT
  HCS08
  )
