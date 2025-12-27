add_llvm_componet_group(Epromint)

add_llvm_target(EpromintCodeGen
  EpromintTargetMachine.cpp

  LINK_COMPONENTS
  EpromintDesc
  EpromintInfo
  Support

  ADD_TO_COMPONENT
  Epromint
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
