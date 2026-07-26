//===-- mulsi3.c - int32 multiplication -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Shift and add, which is the small and obviously correct version rather than
// the fast one. Decomposing into 16-bit partial products would be several
// times quicker, but it needs a widening 16x16 -> 32 multiply that this target
// does not have and that would itself have to be written in assembly.
//
// Only needed on a target whose int is 16 bits, so compiler-rt has no generic
// version.
//
//===----------------------------------------------------------------------===//

#include "../int_lib.h"

COMPILER_RT_ABI uint32_t __mulsi3(uint32_t a, uint32_t b) {
  uint32_t r = 0;
  while (b) {
    if (b & 1)
      r += a;
    a += a;   // a << 1, without asking for a shift
    b >>= 1;
  }
  return r;
}
