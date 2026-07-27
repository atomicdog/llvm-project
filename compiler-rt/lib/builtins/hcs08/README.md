# HCS08 builtins

Building these needs a cross-compile of the builtins on their own, because
there is no libc and nothing that can link an executable to probe with:

    cmake -S compiler-rt/lib/builtins -B build-rt-hcs08 -G Ninja \
      -DCMAKE_C_COMPILER=<build>/bin/clang \
      -DCMAKE_ASM_COMPILER=<build>/bin/clang \
      -DCMAKE_CXX_COMPILER=<build>/bin/clang++ \
      -DCMAKE_AR=<build>/bin/llvm-ar \
      -DCMAKE_RANLIB=<build>/bin/llvm-ranlib \
      -DCMAKE_C_COMPILER_TARGET=hcs08 \
      -DCMAKE_ASM_COMPILER_TARGET=hcs08 \
      -DCMAKE_CXX_COMPILER_TARGET=hcs08 \
      -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
      -DCOMPILER_RT_BAREMETAL_BUILD=ON \
      -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
      -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=ON \
      -DCOMPILER_RT_OS_DIR=hcs08 \
      -DCMAKE_INSTALL_PREFIX=$(<build>/bin/clang -print-resource-dir) \
      -DLLVM_CONFIG_PATH=<build>/bin/llvm-config \
      -DCMAKE_C_FLAGS="-Os -ffreestanding"
    ninja -C build-rt-hcs08 install

Four of those are load-bearing:

- `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY`, because CMake's usual
  compiler check links an executable and there is nothing to link against.
- A target triple for the **C++** compiler as well. Nothing here is C++, but
  the standalone builtins project declares the language, so CMake probes it.
- `-ffreestanding`. Otherwise clang's `limits.h` chains to the host's through
  `#include_next` and the build compiles against the wrong system.
- `LLVM_ENABLE_PER_TARGET_RUNTIME_DIR=ON` if the toolchain was configured that
  way, which decides whether the archive is named with an architecture suffix.
  Compare `clang -print-libgcc-file-name` against what got installed.

## What is in here, and what is not

The 16-bit routines are written here because the hardware multiply is 8x8 and
the hardware divide is 16/8. Only three need assembly; the rest are C written
so that each function uses only operations narrower than the one it
implements, since `a * b` on two ints is what the compiler turns into a call
to `__mulhi3`.

The generic sources are listed one at a time in `../CMakeLists.txt` rather
than taken wholesale. Most of that set is soft float and complex arithmetic,
which this target has no support for and which exhausts its one accumulator
during register allocation.

**64-bit division builds now.** `udivmoddi4.c` used to exhaust the register
allocator, which took `__udivdi3` and `__umoddi3` with it. That was not
pressure: it was a missing `isLoadFromStackSlot`/`isStoreToStackSlot` pair, so
the spiller could not recognise its own spills. With those in place the whole
set builds with the default allocator.

Two earlier diagnoses in this file were wrong, in a way worth remembering. A
compile-time explosion was blamed first; that was an infinite loop in branch
folding from a bug in `removeBranch`, nothing to do with this target. Then
register pressure was blamed; that was the spiller hooks. Neither was an
argument for a direct-page register file, though both were offered as one.

**Single precision is in and double precision is not.** `addsf3`, `subsf3`,
`mulsf3`, `divsf3`, `comparesf2`, and the conversions both ways are built and
verified against an independent simulator, 144 cases over arithmetic,
subnormals, the comparison predicates, conversions and the calling convention.
`__divdf3` and the rest of the f64 set run past the 256-byte frame ceiling and
will not compile at any setting, and would not fit a 32KB part regardless:
`muldf3` alone is 14KB.

That split is why the clang target defines `__SOFTFP__`. `fixsfdi.c` and
`fixunssfdi.c` each carry two implementations, and the one they choose without
it converts through `double` - so `(long long)someFloat` pulls in `__muldf3`
and `__adddf3` and fails to link. With it they use the integer path, which is
both correct here and a good deal smaller.

`fp_mode.c` is needed too, though nothing calls it directly: `__addsf3` asks
it for the rounding mode. There is no FPU to ask, so it answers
round-to-nearest and drops the inexact flag.
