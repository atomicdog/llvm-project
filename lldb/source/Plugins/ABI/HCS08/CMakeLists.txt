add_lldb_library(lldbPluginABIHCS08 PLUGIN
  ABIHCS08.cpp

  LINK_COMPONENTS
    Support
    TargetParser
  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbValueObject
  )
