//===-- modhi3.c - signed int16 remainder ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../int_lib.h"

// C requires the remainder to take the sign of the dividend.
COMPILER_RT_ABI short __modhi3(short a, short b) {
  unsigned short ua = a < 0 ? -(unsigned short)a : (unsigned short)a;
  unsigned short ub = b < 0 ? -(unsigned short)b : (unsigned short)b;
  unsigned short r = ua % ub;
  return a < 0 ? -(short)r : (short)r;
}
