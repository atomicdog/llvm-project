//===-- shiftsi3.c - int32 shifts by a variable amount --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A 32-bit shift, done on the two halves. Every shift below is 16-bit, which
// the compiler handles in place; writing these as a shift of the 32-bit value
// would be a call back to the function being defined.
//
// These are only needed on a target whose int is 16 bits, so compiler-rt has
// no generic version of them.
//
//===----------------------------------------------------------------------===//

#include "../int_lib.h"

// Big endian, so half[0] is the high half.
typedef union {
  uint32_t all;
  struct {
    uint16_t high;
    uint16_t low;
  } s;
} uword;

typedef union {
  int32_t all;
  struct {
    int16_t high;
    uint16_t low;
  } s;
} sword;

COMPILER_RT_ABI uint32_t __ashlsi3(uint32_t a, int b) {
  uword r;
  r.all = a;
  if (b == 0)
    return a;
  if (b >= 16) {
    r.s.high = r.s.low << (b - 16);
    r.s.low = 0;
  } else {
    r.s.high = (r.s.high << b) | (r.s.low >> (16 - b));
    r.s.low = r.s.low << b;
  }
  return r.all;
}

COMPILER_RT_ABI uint32_t __lshrsi3(uint32_t a, int b) {
  uword r;
  r.all = a;
  if (b == 0)
    return a;
  if (b >= 16) {
    r.s.low = r.s.high >> (b - 16);
    r.s.high = 0;
  } else {
    r.s.low = (r.s.low >> b) | (r.s.high << (16 - b));
    r.s.high = r.s.high >> b;
  }
  return r.all;
}

COMPILER_RT_ABI int32_t __ashrsi3(int32_t a, int b) {
  sword r;
  r.all = a;
  if (b == 0)
    return a;
  if (b >= 16) {
    // The arithmetic shift leaves the high half all sign bits.
    r.s.low = r.s.high >> (b - 16);
    r.s.high = r.s.high >> 15;
  } else {
    r.s.low = (r.s.low >> b) | ((uint16_t)r.s.high << (16 - b));
    r.s.high = r.s.high >> b;
  }
  return r.all;
}
