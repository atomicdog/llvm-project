// RUN: %clang_cc1 -triple hcs08 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple hcs08 -emit-llvm -mdirect-page-bank=8 -o - %s \
// RUN:   | FileCheck %s --check-prefix=BANK

// The attributes only have to reach the backend; what they turn into is
// HCS08FrameLowering's business. See CodeGenDesign.md section 20.

// CHECK: define{{.*}} void @handler() [[ISR:#[0-9]+]]
__attribute__((interrupt)) void handler(void) {}

// CHECK: define{{.*}} void @promised() [[BOTH:#[0-9]+]]
__attribute__((interrupt, no_direct_page_bank)) void promised(void) {}

// CHECK: define{{.*}} void @ordinary() [[PLAIN:#[0-9]+]]
void ordinary(void) {}

// CHECK-DAG: attributes [[ISR]] = {{.*}}"hcs08-interrupt"
// CHECK-DAG: attributes [[BOTH]] = {{.*}}"hcs08-interrupt"{{.*}}"hcs08-no-dp-bank"
// CHECK-NOT: attributes [[PLAIN]] = {{.*}}"hcs08-interrupt"

// -mdirect-page-bank= is a module-wide default, so it lands on every function
// including the handlers; the promise is what the backend consults to override
// it, and getHCS08DPBankSize answers zero when it is present.
// BANK-DAG: "hcs08-direct-page-bank"="8"
// BANK-DAG: "hcs08-no-dp-bank"
