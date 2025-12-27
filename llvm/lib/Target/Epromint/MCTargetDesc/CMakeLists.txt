add_llvm_component_library(LLVMEpromintTargetDesc
  EpromintTargetDesc.cpp

  LINK_COMPONENTS
  Support

  ADD_TO_COMPONENT
  Epromint
)
