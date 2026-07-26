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

**64-bit division is missing.** `udivmoddi4.c` exhausts the register
allocator, and `__udivdi3` and `__umoddi3` are written in terms of it, so
none of the three is built and a 64-bit divide fails to link. Multiplication
and the shifts are here and work.

An earlier version of this file blamed a compile-time explosion for that,
which was wrong twice over: the explosion was an infinite loop in branch
folding caused by a bug in `removeBranch`, and it had nothing to do with this
target's register pressure. With that fixed, `udivmoddi4.c` fails in under a
second with a diagnosis. The remaining failure is genuinely allocation, and
is the strongest argument so far for the direct-page register file in
CodeGenDesign.md.
