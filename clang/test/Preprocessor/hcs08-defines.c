// RUN: %clang_cc1 -E -dM -triple hcs08 %s -o - | FileCheck %s

// CHECK-DAG: #define HCS08 1
// CHECK-DAG: #define __HCS08__ 1
// CHECK-DAG: #define __hcs08__ 1

// A 16-bit int and pointer, and a byte-aligned everything.
// CHECK-DAG: #define __INT_MAX__ 32767
// CHECK-DAG: #define __SIZEOF_POINTER__ 2
// CHECK-DAG: #define __SIZEOF_INT__ 2
// CHECK-DAG: #define __SIZEOF_LONG__ 4
// CHECK-DAG: #define __SIZEOF_LONG_LONG__ 8
// CHECK-DAG: #define __BIGGEST_ALIGNMENT__ 1

// Big endian, which is what sthx stores and ldhx loads.
// CHECK-DAG: #define __BYTE_ORDER__ __ORDER_BIG_ENDIAN__

// double is float. Narrowed because f64 cannot work here at any setting, so
// 64 bits bought a link error rather than conformance; it also makes a float
// through "..." work, which C promotes to double. SDCC does the same for these
// parts and avr-gcc spells it -fshort-double. Section 22.
// CHECK-DAG: #define __SIZEOF_DOUBLE__ 4
// CHECK-DAG: #define __SIZEOF_LONG_DOUBLE__ 4
// CHECK-DAG: #define __DBL_MANT_DIG__ 24
// CHECK-DAG: #define __LDBL_MANT_DIG__ 24
// CHECK-DAG: #define __DBL_MAX_EXP__ 128

// No FPU, so every float operation is a call into the runtime. compiler-rt
// reads this to decide whether it may convert through double: __fixsfdi and
// __fixunssfdi do exactly that when it is missing, which needs __muldf3 and
// __adddf3, and no double-precision routine fits this target's frame.
// CHECK-DAG: #define __SOFTFP__ 1
