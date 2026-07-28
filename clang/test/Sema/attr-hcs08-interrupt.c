// RUN: %clang_cc1 -triple hcs08 -fsyntax-only -verify %s

// Nothing calls a handler, so there is nowhere for arguments to come from or a
// result to go: the hardware provides the one and discards the other. It is
// more than tidiness - an incoming stack argument is addressed past a two-byte
// return address, and an interrupt frame is five bytes, so an argument would be
// read from the wrong place.

__attribute__((interrupt)) void ok(void) {}

__attribute__((interrupt)) int returns_value(void) { // expected-warning {{HCS08 'interrupt' attribute only applies to functions that have a 'void' return type}}
  return 1;
}

__attribute__((interrupt)) void takes_args(int x) { // expected-warning {{HCS08 'interrupt' attribute only applies to functions that have no parameters}}
  (void)x;
}

// Which vector a handler serves is the linker script's business, so unlike
// MSP430's the attribute takes no number.
__attribute__((interrupt(3))) void numbered(void) {} // expected-error {{'interrupt' attribute takes no arguments}}

__attribute__((interrupt)) int not_a_function; // expected-warning {{'interrupt' attribute only applies to functions}}

// The bank promise is a function-level thing too, but it is meaningful on any
// function - it keeps that function out of the bank - so there is no
// requirement that it accompany 'interrupt'.
__attribute__((no_direct_page_bank)) void nobank(void) {}
__attribute__((interrupt, no_direct_page_bank)) void both(void) {}

__attribute__((no_direct_page_bank)) int nb_not_a_function; // expected-error {{'no_direct_page_bank' attribute only applies to functions}}
