add_llvm_component_library(LLVMEpromintInfo
  EpromintTargetInfo.cpp

  LINK_COMPONENTS
  Support

  ADD_TO_COMPONENT
  Epromint
)
