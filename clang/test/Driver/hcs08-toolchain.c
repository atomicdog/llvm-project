// A bare-metal target: one static image, lld as the only linker that knows the
// relocations, and no system libraries to pick up.

// RUN: %clang --target=hcs08 -### %s -fuse-ld=lld 2>&1 \
// RUN:   | FileCheck -check-prefix=LINK %s
// LINK: "-cc1" "-triple" "hcs08"
// LINK: ld.lld
// LINK-SAME: "-m" "hcs08elf"
// LINK-SAME: "-Bstatic"

// A linker script is the user's to supply: RAM and flash differ across the
// family, and a guessed memory map would link and then not run.
// RUN: %clang --target=hcs08 -### %s -fuse-ld=lld -T mem.ld 2>&1 \
// RUN:   | FileCheck -check-prefix=SCRIPT %s
// SCRIPT: ld.lld
// SCRIPT-SAME: "-T" "mem.ld"

// The runtime library is found the usual compiler-rt way. It does not exist
// yet, so until it does a link needs -nostdlib.
// RUN: %clang --target=hcs08 -### %s -fuse-ld=lld 2>&1 \
// RUN:   | FileCheck -check-prefix=RTLIB %s
// RTLIB: libclang_rt.builtins.a

// RUN: %clang --target=hcs08 -### %s -fuse-ld=lld -nostdlib 2>&1 \
// RUN:   | FileCheck -check-prefix=NOSTDLIB %s
// NOSTDLIB: ld.lld
// NOSTDLIB-NOT: libclang_rt

// Nothing position-independent: there is no loader.
// RUN: %clang --target=hcs08 -### -c %s 2>&1 \
// RUN:   | FileCheck -check-prefix=NOPIC %s
// NOPIC-NOT: "-mrelocation-model" "pic"

void _start(void) {}
