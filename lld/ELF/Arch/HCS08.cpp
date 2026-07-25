//===- HCS08.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The HCS08 is an 8-bit Freescale microcontroller core with a 16-bit address
// space, one accumulator and one index register. There is no published psABI,
// so the relocation set is LLVM-defined and small: an absolute byte, an
// absolute big-endian word, a signed byte of PC-relative branch displacement,
// and the two halves of an address for materializing it through the 8-bit
// accumulator.
//
// Everything must agree with HCS08AsmBackend, which resolves whatever it can
// before the linker sees it.
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class HCS08 final : public TargetInfo {
public:
  HCS08(Ctx &);
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

HCS08::HCS08(Ctx &ctx) : TargetInfo(ctx) {
  // "bra ." - a branch to itself. Reaching padding is a bug either way, and
  // spinning is kinder than bgnd, which halts a part that has no debugger
  // attached.
  trapInstr = {0x20, 0xfe, 0x20, 0xfe};
}

RelExpr HCS08::getRelExpr(RelType type, const Symbol &s,
                          const uint8_t *loc) const {
  switch (type) {
  case R_HCS08_PCREL_8:
    return R_PC;
  case R_HCS08_NONE:
    return R_NONE;
  default:
    return R_ABS;
  }
}

void HCS08::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_HCS08_NONE:
    break;

  case R_HCS08_8:
    checkIntUInt(ctx, loc, val, 8, rel);
    *loc = val;
    break;

  case R_HCS08_16:
    checkIntUInt(ctx, loc, val, 16, rel);
    write16be(loc, val);
    break;

  case R_HCS08_PCREL_8: {
    // A branch displacement is measured from the end of the instruction, and
    // the relocated byte is the last one, so the base is one past it.
    int64_t disp = static_cast<int64_t>(val) - 1;
    checkInt(ctx, loc, disp, 8, rel);
    *loc = disp;
    break;
  }

  // Either half of an address is a valid byte, so neither is range-checked.
  case R_HCS08_HI8:
    *loc = (val >> 8) & 0xff;
    break;

  case R_HCS08_LO8:
    *loc = val & 0xff;
    break;

  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << rel.type;
  }
}

void elf::setHCS08TargetInfo(Ctx &ctx) { ctx.target.reset(new HCS08(ctx)); }
