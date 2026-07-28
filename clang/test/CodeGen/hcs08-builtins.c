// RUN: %clang_cc1 -triple hcs08 -emit-llvm -o - %s | FileCheck %s

// The builtins reach their intrinsics through the ClangBuiltin<> on each one,
// so there is no HCS08 case in CGBuiltin and nothing here but the mapping.

// CHECK-LABEL: define{{.*}} void @mask()
// CHECK: call void @llvm.hcs08.sei()
// CHECK: call void @llvm.hcs08.cli()
void mask(void) {
  __builtin_hcs08_sei();
  __builtin_hcs08_cli();
}

// CHECK-LABEL: define{{.*}} void @sleep()
// CHECK: call void @llvm.hcs08.wait()
// CHECK: call void @llvm.hcs08.stop()
void sleep(void) {
  __builtin_hcs08_wait();
  __builtin_hcs08_stop();
}

// CHECK-LABEL: define{{.*}} void @pad()
// CHECK: call void @llvm.hcs08.nop()
void pad(void) { __builtin_hcs08_nop(); }

// The composable critical section: sei and cli alone cannot nest, because a
// function ending its section with cli unmasks even when its caller had
// masked.
//
// CHECK-LABEL: define{{.*}} void @critical()
// CHECK: [[S:%.*]] = call i8 @llvm.hcs08.get.ccr()
// CHECK: call void @llvm.hcs08.sei()
// CHECK: call void @llvm.hcs08.set.ccr(i8 {{.*}})
void critical(void) {
  unsigned char s = __builtin_hcs08_get_ccr();
  __builtin_hcs08_sei();
  __builtin_hcs08_set_ccr(s);
}
