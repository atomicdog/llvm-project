//===-- HCS08.td - Describe the HCS08 Target Machine -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Top level entry point for the HCS08 target.
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Subtarget features
//===----------------------------------------------------------------------===//

// The HCS08 core adds the 16-bit index register instructions, the
// memory-to-memory move, hardware divide and the background-debug entry to the
// original HC08 map. Everything here is currently accepted unconditionally;
// the feature exists so that an HC08-only subtarget can reject the additions.
def FeatureHCS08
    : SubtargetFeature<"hcs08", "HasHCS08", "true",
                       "Enable HCS08 core instructions">;

//===----------------------------------------------------------------------===//
// Processors
//===----------------------------------------------------------------------===//

class Proc<string Name, list<SubtargetFeature> Features>
    : Processor<Name, NoItineraries, Features>;

def : Proc<"generic", [FeatureHCS08]>;
def : Proc<"hc08", []>;
def : Proc<"mc9s08", [FeatureHCS08]>;
def : Proc<"hcs08", [FeatureHCS08]>;

//===----------------------------------------------------------------------===//
// Register file, instructions
//===----------------------------------------------------------------------===//

include "HCS08RegisterInfo.td"
include "HCS08InstrInfo.td"

// Pointers are 16 bit, so the target-independent pseudo instructions that take
// a PointerLikeRegClass operand resolve to the 16-bit class.
defm : RemapAllTargetPseudoPointerOperands<GR16>;

def HCS08InstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// Assembly printer / parser
//===----------------------------------------------------------------------===//

def HCS08AsmWriter : AsmWriter {
  string AsmWriterClassName = "InstPrinter";
}

def HCS08AsmParser : AsmParser;

def HCS08AsmParserVariant : AsmParserVariant {
  let RegisterPrefix = "";
}

//===----------------------------------------------------------------------===//
// Target declaration
//===----------------------------------------------------------------------===//

def HCS08 : Target {
  let InstructionSet = HCS08InstrInfo;
  let AssemblyWriters = [HCS08AsmWriter];
  let AssemblyParsers = [HCS08AsmParser];
  let AssemblyParserVariants = [HCS08AsmParserVariant];
}
