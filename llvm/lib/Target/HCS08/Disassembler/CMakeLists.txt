add_llvm_component_library(LLVMHCS08Disassembler
  HCS08Disassembler.cpp

  LINK_COMPONENTS
  MC
  MCDisassembler
  HCS08Info
  Support

  ADD_TO_COMPONENT
  HCS08
  )
