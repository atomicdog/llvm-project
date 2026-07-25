//===-- HCS08InstrFormats.td - HCS08 Instruction Formats -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// HCS08 instruction format classes.
//
// HCS08 instructions are 1 to 4 bytes: an opcode byte, optionally preceded by
// the 0x9E page-2 prefix, followed by 0-2 big-endian operand bytes. The opcode
// byte alone determines both the addressing mode and the total length, so the
// disassembler can try the fixed-length decoder tables in increasing size
// order without ambiguity.
//
// Encodings are laid out so that the first byte emitted occupies the most
// significant bits of Inst; HCS08MCCodeEmitter then writes Size bytes
// starting from the top.
//
//===----------------------------------------------------------------------===//

// Addressing modes, recorded in TSFlags for the disassembler and for later
// codegen use.
class AddrMode<bits<5> val> { bits<5> Value = val; }
def AddrModeNone : AddrMode<0>;  // no operand
def AddrModeInh  : AddrMode<1>;  // inherent
def AddrModeImm  : AddrMode<2>;  // #$nn / #$nnnn
def AddrModeDir  : AddrMode<3>;  // $nn          direct page
def AddrModeExt  : AddrMode<4>;  // $nnnn        extended
def AddrModeIx   : AddrMode<5>;  // ,x
def AddrModeIx1  : AddrMode<6>;  // $nn,x
def AddrModeIx2  : AddrMode<7>;  // $nnnn,x
def AddrModeIxP  : AddrMode<8>;  // ,x+
def AddrModeSp1  : AddrMode<9>;  // $nn,sp
def AddrModeSp2  : AddrMode<10>; // $nnnn,sp
def AddrModeRel  : AddrMode<11>; // branch displacement
def AddrModeDirRel : AddrMode<12>; // $nn,rr
def AddrModeImd  : AddrMode<13>; // #$nn,$dd
def AddrModeDd   : AddrMode<14>; // $ss,$dd

class HCS08Inst<int size, AddrMode am, dag outs, dag ins, string asmstr,
                 list<dag> pattern>
    : Instruction {
  let Namespace = "HCS08";
  let Size = size;
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;

  AddrMode Am = am;
  let TSFlags{4-0} = Am.Value;
}

//===----------------------------------------------------------------------===//
// One byte: opcode only.
//===----------------------------------------------------------------------===//

class F8<bits<8> op, AddrMode am, dag outs, dag ins, string asmstr,
         list<dag> pattern = []>
    : HCS08Inst<1, am, outs, ins, asmstr, pattern> {
  field bits<8> Inst;
  field bits<8> SoftFail = 0;
  let Inst{7-0} = op;
}

//===----------------------------------------------------------------------===//
// Two bytes: opcode + one operand byte, or the 0x9E prefix + opcode.
//===----------------------------------------------------------------------===//

class F16<bits<8> op, AddrMode am, dag outs, dag ins, string asmstr,
          list<dag> pattern = []>
    : HCS08Inst<2, am, outs, ins, asmstr, pattern> {
  bits<8> b0;
  field bits<16> Inst;
  field bits<16> SoftFail = 0;
  let Inst{15-8} = op;
  let Inst{7-0} = b0;
}

// 0x9E-prefixed instruction with no operand bytes (only 9E AE, "ldhx ,x").
class F16P<bits<8> op, AddrMode am, dag outs, dag ins, string asmstr,
           list<dag> pattern = []>
    : HCS08Inst<2, am, outs, ins, asmstr, pattern> {
  field bits<16> Inst;
  field bits<16> SoftFail = 0;
  let Inst{15-8} = 0x9E;
  let Inst{7-0} = op;
}

//===----------------------------------------------------------------------===//
// Three bytes.
//===----------------------------------------------------------------------===//

// opcode + 16-bit big-endian operand.
class F24<bits<8> op, AddrMode am, dag outs, dag ins, string asmstr,
          list<dag> pattern = []>
    : HCS08Inst<3, am, outs, ins, asmstr, pattern> {
  bits<16> w0;
  field bits<24> Inst;
  field bits<24> SoftFail = 0;
  let Inst{23-16} = op;
  let Inst{15-0} = w0;
}

// opcode + two independent operand bytes ($nn,rr and the MOV forms).
class F24B<bits<8> op, AddrMode am, dag outs, dag ins, string asmstr,
           list<dag> pattern = []>
    : HCS08Inst<3, am, outs, ins, asmstr, pattern> {
  bits<8> b0;
  bits<8> b1;
  field bits<24> Inst;
  field bits<24> SoftFail = 0;
  let Inst{23-16} = op;
  let Inst{15-8} = b0;
  let Inst{7-0} = b1;
}

// 0x9E prefix + opcode + one operand byte.
class F24P<bits<8> op, AddrMode am, dag outs, dag ins, string asmstr,
           list<dag> pattern = []>
    : HCS08Inst<3, am, outs, ins, asmstr, pattern> {
  bits<8> b0;
  field bits<24> Inst;
  field bits<24> SoftFail = 0;
  let Inst{23-16} = 0x9E;
  let Inst{15-8} = op;
  let Inst{7-0} = b0;
}

//===----------------------------------------------------------------------===//
// Four bytes, always 0x9E-prefixed.
//===----------------------------------------------------------------------===//

// 0x9E + opcode + 16-bit big-endian operand (SP2, and "ldhx $nnnn,x").
class F32P<bits<8> op, AddrMode am, dag outs, dag ins, string asmstr,
           list<dag> pattern = []>
    : HCS08Inst<4, am, outs, ins, asmstr, pattern> {
  bits<16> w0;
  field bits<32> Inst;
  field bits<32> SoftFail = 0;
  let Inst{31-24} = 0x9E;
  let Inst{23-16} = op;
  let Inst{15-0} = w0;
}

// 0x9E + opcode + two independent operand bytes ($nn,sp,rr forms).
class F32PB<bits<8> op, AddrMode am, dag outs, dag ins, string asmstr,
            list<dag> pattern = []>
    : HCS08Inst<4, am, outs, ins, asmstr, pattern> {
  bits<8> b0;
  bits<8> b1;
  field bits<32> Inst;
  field bits<32> SoftFail = 0;
  let Inst{31-24} = 0x9E;
  let Inst{23-16} = op;
  let Inst{15-8} = b0;
  let Inst{7-0} = b1;
}
