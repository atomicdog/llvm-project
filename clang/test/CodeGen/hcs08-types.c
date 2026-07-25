// RUN: %clang_cc1 -triple hcs08 -emit-llvm -o - %s | FileCheck %s

// The type model: a 16-bit int matching the index register and the pointer
// width, and no alignment requirement anywhere. Nothing on this machine cares
// about alignment - every access is a byte at a time or an index register pair
// - and padding would only make objects bigger on a part with a few hundred
// bytes of RAM.

// CHECK: target datalayout = "E-m:e-p:16:16-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-a:0:8-n8:16-S8"
// CHECK: target triple = "hcs08"

_Static_assert(sizeof(char) == 1, "");
_Static_assert(sizeof(short) == 2, "");
_Static_assert(sizeof(int) == 2, "");
_Static_assert(sizeof(long) == 4, "");
_Static_assert(sizeof(long long) == 8, "");
_Static_assert(sizeof(void *) == 2, "");
_Static_assert(sizeof(float) == 4, "");
_Static_assert(sizeof(double) == 8, "");

_Static_assert(_Alignof(int) == 1, "");
_Static_assert(_Alignof(long) == 1, "");
_Static_assert(_Alignof(long long) == 1, "");
_Static_assert(_Alignof(void *) == 1, "");
_Static_assert(_Alignof(double) == 1, "");

// A struct is packed tight, with no padding between a char and an int.
struct S {
  char c;
  int i;
};
_Static_assert(sizeof(struct S) == 3, "");

// CHECK-LABEL: define {{.*}} i16 @add(
// CHECK: add nsw i16
int add(int a, int b) { return a + b; }

// CHECK-LABEL: define {{.*}} i8 @deref(
// CHECK: load i8
char deref(char *p) { return *p; }
