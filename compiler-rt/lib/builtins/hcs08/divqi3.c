//===-- divqi3.c - signed int8 division -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../int_lib.h"

// The unsigned byte divide is the hardware div instruction, so this expands
// in place rather than calling anything.
COMPILER_RT_ABI signed char __divqi3(signed char a, signed char b) {
  unsigned char ua = a < 0 ? -(unsigned char)a : (unsigned char)a;
  unsigned char ub = b < 0 ? -(unsigned char)b : (unsigned char)b;
  unsigned char q = ua / ub;
  return (a ^ b) < 0 ? -(signed char)q : (signed char)q;
}
