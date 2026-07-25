//===-- HCS08MCTargetDesc.h - HCS08 Target Descriptions -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides HCS08 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HCS08_MCTARGETDESC_HCS08MCTARGETDESC_H
#define LLVM_LIB_TARGET_HCS08_MCTARGETDESC_HCS08MCTARGETDESC_H

#include "llvm/Support/DataTypes.h"
#include <memory>

namespace llvm {

class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCTargetOptions;
class Target;

MCCodeEmitter *createHCS08MCCodeEmitter(const MCInstrInfo &MCII,
                                         MCContext &Ctx);

MCAsmBackend *createHCS08MCAsmBackend(const Target &T,
                                       const MCSubtargetInfo &STI,
                                       const MCRegisterInfo &MRI,
                                       const MCTargetOptions &Options);

std::unique_ptr<MCObjectTargetWriter>
createHCS08ELFObjectWriter(uint8_t OSABI);

} // end namespace llvm

// Defines symbolic names for HCS08 registers.
#define GET_REGINFO_ENUM
#include "HCS08GenRegisterInfo.inc"

// Defines symbolic names for the HCS08 instructions.
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "HCS08GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "HCS08GenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_HCS08_MCTARGETDESC_HCS08MCTARGETDESC_H
