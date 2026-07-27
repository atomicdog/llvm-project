// How much of the direct page the compiler may spend on its spill bank. The
// page is the board's, not the program's - the low end is memory-mapped
// registers and how much RAM follows differs across the family - so the
// compiler never picks a size, and the linker script has to reserve the same
// number at __hcs08_dp_bank.

// RUN: %clang -### -target hcs08 -mdirect-page-bank=8 -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=SET
// SET: "-mdirect-page-bank=8"

// Absent, it is off: the direct page stays entirely the user's.
// RUN: %clang -### -target hcs08 -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=UNSET
// UNSET-NOT: "-mdirect-page-bank=

// Zero is the same as absent, and is spelled out rather than rejected so that
// a build system can pass a computed size without a special case for none.
// RUN: %clang -### -target hcs08 -mdirect-page-bank=0 -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=ZERO
// ZERO: "-mdirect-page-bank=0"

// A bank larger than the page cannot be addressed by the direct-page forms at
// all, so it is refused here rather than at link time.
// RUN: not %clang -target hcs08 -mdirect-page-bank=257 -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=TOOBIG
// TOOBIG: invalid integral value '257' in '-mdirect-page-bank=257'

// RUN: not %clang -target hcs08 -mdirect-page-bank=eight -c %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=NAN
// NAN: invalid integral value 'eight' in '-mdirect-page-bank=eight'

// And it reaches the IR as a function attribute, which is what the backend
// reads - one per function, so it survives inlining and LTO the way a global
// option would not.
// RUN: %clang -target hcs08 -mdirect-page-bank=8 -S -emit-llvm -o - %s 2>&1 | \
// RUN:   FileCheck %s --check-prefix=ATTR
// ATTR: attributes #0 = {{.*}}"hcs08-direct-page-bank"="8"

unsigned long add32(unsigned long a, unsigned long b) { return a + b; }
