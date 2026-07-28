//===- IntrinsicsHCS08.td - Defines HCS08 intrinsics --------*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the HCS08-specific intrinsics.
//
// These are the instructions with no expression in C: they act on the CPU
// itself rather than on a value, so there is nothing for the front end to
// constant-fold or for the optimiser to reassociate. Each carries a
// ClangBuiltin, which is all the mapping clang needs - the generic path in
// CGBuiltin looks the name up under the target's arch prefix, so there is no
// per-builtin code anywhere.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "hcs08" in { // All intrinsics start with "llvm.hcs08.".

  // Interrupt masking. Deliberately NOT IntrNoMem: with it, a load or store
  // could be hoisted out of the region these two delimit, which is the one
  // thing a critical section exists to prevent. Leaving memory unmodelled
  // makes each a full barrier, which is what the hardware bit means.
  //
  // Note the sense, which is the opposite of the one the mnemonics suggest to
  // anyone arriving from another architecture: on this machine I is a *mask*,
  // so sei disables interrupts and cli enables them.
  def int_hcs08_sei : ClangBuiltin<"__builtin_hcs08_sei">,
                      Intrinsic<[], [], [IntrHasSideEffects]>;
  def int_hcs08_cli : ClangBuiltin<"__builtin_hcs08_cli">,
                      Intrinsic<[], [], [IntrHasSideEffects]>;

  // The condition code register, whole. sei and cli alone cannot nest - a
  // function that ends its critical section with cli enables interrupts even
  // when its caller had them masked - so the composable form is to save the
  // register and put it back:
  //
  //     unsigned char s = __builtin_hcs08_get_ccr();
  //     __builtin_hcs08_sei();
  //     ... critical section ...
  //     __builtin_hcs08_set_ccr(s);
  //
  // Only the control bits round-trip meaningfully. The condition codes in the
  // result are whatever the last flag-setting instruction happened to leave,
  // and the compiler is free to schedule ordinary arithmetic around these, so
  // do not read carry out of one and expect it back from the other.
  def int_hcs08_get_ccr : ClangBuiltin<"__builtin_hcs08_get_ccr">,
                          Intrinsic<[llvm_i8_ty], [], [IntrHasSideEffects]>;
  def int_hcs08_set_ccr : ClangBuiltin<"__builtin_hcs08_set_ccr">,
                          Intrinsic<[], [llvm_i8_ty], [IntrHasSideEffects]>;

  // Stop the clocks until an interrupt arrives. `cli` then `wait` is the whole
  // of an idle loop on this part; the pair is not atomic, so an interrupt
  // landing between them is serviced and the wait is entered afterwards.
  //
  // stop is deeper and part-specific: on most MC9S08 parts it is disabled out
  // of reset and enabling it is a write to SOPT, so a program that has not
  // done that will find this instruction behaves as an illegal opcode.
  def int_hcs08_wait : ClangBuiltin<"__builtin_hcs08_wait">,
                       Intrinsic<[], [], [IntrHasSideEffects]>;
  def int_hcs08_stop : ClangBuiltin<"__builtin_hcs08_stop">,
                       Intrinsic<[], [], [IntrHasSideEffects]>;

  // One byte, one cycle, no effect - for padding a timing loop. IntrNoMem
  // because unlike the others it really does touch nothing; it still may not
  // be deleted, hence the side effect.
  def int_hcs08_nop : ClangBuiltin<"__builtin_hcs08_nop">,
                      Intrinsic<[], [], [IntrNoMem, IntrHasSideEffects]>;
}
