add_llvm_component_library(LLVMHCS08AsmParser
  HCS08AsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  HCS08Desc
  HCS08Info
  Support

  ADD_TO_COMPONENT
  HCS08
  )
