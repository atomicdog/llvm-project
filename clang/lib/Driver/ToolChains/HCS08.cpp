//===--- HCS08.cpp - HCS08 ToolChain Implementations ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HCS08.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Options/Options.h"
#include "llvm/Option/ArgList.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

HCS08ToolChain::HCS08ToolChain(const Driver &D, const llvm::Triple &Triple,
                               const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  // No system library directories are added. There is no system here to have a
  // /usr/lib, and picking up a host library would be worse than failing to
  // find one. The runtime library is found the usual compiler-rt way, at
  // <resource-dir>/lib/hcs08/libclang_rt.builtins.a.
}

Tool *HCS08ToolChain::buildLinker() const {
  return new tools::hcs08::Linker(*this);
}

void hcs08::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                 const InputInfo &Output,
                                 const InputInfoList &Inputs,
                                 const ArgList &Args,
                                 const char *LinkingOutput) const {
  const ToolChain &TC = getToolChain();
  ArgStringList CmdArgs;

  CmdArgs.push_back("-m");
  CmdArgs.push_back("hcs08elf");

  // Everything is one static image; there is nothing to relax and nothing to
  // load it.
  CmdArgs.push_back("-Bstatic");

  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  }

  // There is no default memory map: RAM and flash sizes and addresses differ
  // across the family, and guessing one would produce an image that links and
  // does not run. A linker script is the user's to supply.
  Args.AddAllArgs(CmdArgs, options::OPT_T);
  Args.AddAllArgs(CmdArgs, options::OPT_L);
  TC.AddFilePathLibArgs(Args, CmdArgs);

  AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs,
                   options::OPT_r))
    AddRunTimeLibs(TC, TC.getDriver(), CmdArgs, Args);

  Args.AddAllArgs(CmdArgs, options::OPT_Wl_COMMA);

  const char *Exec = Args.MakeArgString(TC.GetLinkerPath());
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Exec, CmdArgs, Inputs, Output));
}
