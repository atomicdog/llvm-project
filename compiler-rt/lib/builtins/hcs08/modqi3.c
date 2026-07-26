//===-- modqi3.c - signed int8 remainder ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../int_lib.h"

COMPILER_RT_ABI signed char __modqi3(signed char a, signed char b) {
  unsigned char ua = a < 0 ? -(unsigned char)a : (unsigned char)a;
  unsigned char ub = b < 0 ? -(unsigned char)b : (unsigned char)b;
  unsigned char r = ua % ub;
  return a < 0 ? -(signed char)r : (signed char)r;
}
