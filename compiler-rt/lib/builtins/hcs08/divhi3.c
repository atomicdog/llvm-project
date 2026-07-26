//===-- divhi3.c - signed int16 division ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../int_lib.h"

// Sign fixup around the unsigned divide. Writing this as "a / b" on two ints
// would be a call to itself; going through unsigned reaches __udivhi3 instead.
COMPILER_RT_ABI short __divhi3(short a, short b) {
  unsigned short ua = a < 0 ? -(unsigned short)a : (unsigned short)a;
  unsigned short ub = b < 0 ? -(unsigned short)b : (unsigned short)b;
  unsigned short q = ua / ub;
  return (a ^ b) < 0 ? -(short)q : (short)q;
}
