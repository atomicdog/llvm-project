//===-- HCS08FixupKinds.h - HCS08 Specific Fixup Entries ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HCS08_MCTARGETDESC_HCS08FIXUPKINDS_H
#define LLVM_LIB_TARGET_HCS08_MCTARGETDESC_HCS08FIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

#undef HCS08

namespace llvm {
namespace HCS08 {

// This table must be kept in the same order as the Infos array in
// HCS08AsmBackend.cpp.
enum Fixups {
  // 8-bit absolute: direct-page address or 8-bit immediate.
  fixup_8 = FirstTargetFixupKind,
  // 16-bit absolute, big endian: extended address or 16-bit displacement.
  fixup_16,
  // 8-bit signed PC-relative branch displacement, measured from the end of the
  // instruction.
  fixup_pcrel_8,
  // High and low byte of a 16-bit value, produced by the '#>expr' and '#<expr'
  // immediate modifiers.
  fixup_hi8,
  fixup_lo8,

  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};

} // end namespace HCS08
} // end namespace llvm

#endif // LLVM_LIB_TARGET_HCS08_MCTARGETDESC_HCS08FIXUPKINDS_H
