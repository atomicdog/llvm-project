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
