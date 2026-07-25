add_llvm_component_library(LLVMHCS08Info
  HCS08TargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  HCS08
  )
