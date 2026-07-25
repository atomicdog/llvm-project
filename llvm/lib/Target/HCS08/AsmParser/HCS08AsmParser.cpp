//===-- HCS08AsmParser.cpp - Parse HCS08 assembly ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HCS08MCTargetDesc.h"
#include "TargetInfo/HCS08TargetInfo.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define DEBUG_TYPE "hcs08-asm-parser"

namespace {

/// A parsed HCS08 assembly operand.
///
/// The HCS08 addressing modes are spelled out in the mnemonic's operand list
/// rather than through operands proper ("lda $10,x", "sta 3,sp"). The matcher
/// recognizes the bare "x" and "sp" in those asm strings as the registers of
/// the same name and checks them as singleton registers, so they are parsed as
/// registers here even though no instruction takes a register operand.
class HCS08Operand : public MCParsedAsmOperand {
  enum KindTy { k_Token, k_Immediate, k_Register } Kind;

  SMLoc Start, End;
  StringRef Tok;
  const MCExpr *Imm = nullptr;
  MCRegister Reg;

public:
  HCS08Operand(StringRef T, SMLoc S)
      : Kind(k_Token), Start(S), End(S), Tok(T) {}
  HCS08Operand(const MCExpr *E, SMLoc S, SMLoc E2)
      : Kind(k_Immediate), Start(S), End(E2), Imm(E) {}
  HCS08Operand(MCRegister R, SMLoc S, SMLoc E2)
      : Kind(k_Register), Start(S), End(E2), Reg(R) {}

  bool isToken() const override { return Kind == k_Token; }
  bool isImm() const override { return Kind == k_Immediate; }
  bool isReg() const override { return Kind == k_Register; }
  bool isMem() const override { return false; }

  MCRegister getReg() const override {
    assert(Kind == k_Register && "not a register");
    return Reg;
  }

  StringRef getToken() const {
    assert(Kind == k_Token && "not a token");
    return Tok;
  }

  const MCExpr *getImm() const {
    assert(Kind == k_Immediate && "not an immediate");
    return Imm;
  }

  SMLoc getStartLoc() const override { return Start; }
  SMLoc getEndLoc() const override { return End; }

  /// Matches an operand that is known to fit in a byte, which selects the
  /// direct-page and 8-bit-displacement forms.
  bool isUImm8() const {
    if (Kind != k_Immediate)
      return false;
    int64_t Value;
    if (!Imm->evaluateAsAbsolute(Value))
      return false;
    return Value >= -128 && Value <= 255;
  }

  /// Matches anything representable in a word, including symbols whose value
  /// is not known yet; those always select the extended form.
  bool isUImm16() const {
    if (Kind != k_Immediate)
      return false;
    int64_t Value;
    if (!Imm->evaluateAsAbsolute(Value))
      return true;
    return Value >= -32768 && Value <= 65535;
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "invalid number of operands");
    if (const auto *CE = dyn_cast<MCConstantExpr>(Imm))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Imm));
  }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "invalid number of operands");
    Inst.addOperand(MCOperand::createReg(Reg));
  }

  void print(raw_ostream &OS, const MCAsmInfo &MAI) const override {
    switch (Kind) {
    case k_Token:
      OS << "Token:" << Tok;
      break;
    case k_Immediate:
      OS << "Imm:";
      MAI.printExpr(OS, *Imm);
      break;
    case k_Register:
      OS << "Reg:" << Reg.id();
      break;
    }
  }

  static std::unique_ptr<HCS08Operand> createToken(StringRef Str, SMLoc S) {
    return std::make_unique<HCS08Operand>(Str, S);
  }

  static std::unique_ptr<HCS08Operand> createImm(const MCExpr *Val, SMLoc S,
                                                  SMLoc E) {
    return std::make_unique<HCS08Operand>(Val, S, E);
  }

  static std::unique_ptr<HCS08Operand> createReg(MCRegister R, SMLoc S,
                                                  SMLoc E) {
    return std::make_unique<HCS08Operand>(R, S, E);
  }
};

class HCS08AsmParser : public MCTargetAsmParser {
  MCAsmParser &Parser;

  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override {
    return true;
  }

  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override {
    return ParseStatus::NoMatch;
  }

  /// Parse one comma-separated operand.
  ///
  /// \p BitNumber is set for the first operand of bset/bclr/brset/brclr, where
  /// the bit index is folded into the opcode and so has to be handed to the
  /// matcher as a literal token rather than an immediate.
  bool parseOperand(OperandVector &Operands, bool BitNumber);

  MCAsmParser &getParser() const { return Parser; }
  AsmLexer &getLexer() const { return Parser.getLexer(); }

#define GET_ASSEMBLER_HEADER
#include "HCS08GenAsmMatcher.inc"

public:
  enum HCS08MatchResultTy {
    Match_First = FIRST_TARGET_MATCH_RESULT_TY,
#define GET_OPERAND_DIAGNOSTIC_TYPES
#include "HCS08GenAsmMatcher.inc"
  };

  HCS08AsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                  const MCInstrInfo &MII)
      : MCTargetAsmParser(STI, MII), Parser(Parser) {
    MCAsmParserExtension::Initialize(Parser);
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};

bool HCS08AsmParser::parseOperand(OperandVector &Operands, bool BitNumber) {
  SMLoc S = getLexer().getLoc();

  // "#$nn" - the '#' is a literal token in the matcher's view of the asm
  // string, so it has to be pushed as one here too.
  if (getLexer().is(AsmToken::Hash)) {
    Operands.push_back(HCS08Operand::createToken("#", S));
    getParser().Lex();

    const MCExpr *Val;
    SMLoc ExprLoc = getLexer().getLoc();
    if (getParser().parseExpression(Val))
      return Error(ExprLoc, "expected expression after '#'");
    Operands.push_back(
        HCS08Operand::createImm(Val, ExprLoc, getLexer().getLoc()));
    return false;
  }

  // The bit index of bset/bclr/brset/brclr is part of the mnemonic.
  if (BitNumber && getLexer().is(AsmToken::Integer)) {
    int64_t Value = getLexer().getTok().getIntVal();
    if (Value < 0 || Value > 7)
      return Error(S, "bit number must be in the range 0-7");
    // The matcher compares against the digit spelled in the asm string.
    static const char *const Digits[] = {"0", "1", "2", "3", "4", "5", "6", "7"};
    Operands.push_back(HCS08Operand::createToken(Digits[Value], S));
    getParser().Lex();
    return false;
  }

  // Addressing-mode words: ",x", ",x+" and ",sp". The post-increment form is
  // not a register name, so it stays a literal token; the other two are
  // matched as singleton registers.
  if (getLexer().is(AsmToken::Identifier)) {
    StringRef Id = getLexer().getTok().getIdentifier();
    if (Id.equals_insensitive("x")) {
      SMLoc E = getLexer().getTok().getEndLoc();
      getParser().Lex();
      if (parseOptionalToken(AsmToken::Plus)) {
        Operands.push_back(HCS08Operand::createToken("x+", S));
        return false;
      }
      Operands.push_back(HCS08Operand::createReg(HCS08::X, S, E));
      return false;
    }
    if (Id.equals_insensitive("sp")) {
      SMLoc E = getLexer().getTok().getEndLoc();
      getParser().Lex();
      Operands.push_back(HCS08Operand::createReg(HCS08::SP, S, E));
      return false;
    }
  }

  const MCExpr *Val;
  if (getParser().parseExpression(Val))
    return Error(S, "expected operand");
  Operands.push_back(HCS08Operand::createImm(Val, S, getLexer().getLoc()));
  return false;
}

bool HCS08AsmParser::parseInstruction(ParseInstructionInfo &Info,
                                       StringRef Name, SMLoc NameLoc,
                                       OperandVector &Operands) {
  // Mnemonics are spelled in lower case in the asm strings.
  std::string Lower = Name.lower();
  StringRef Mnemonic(Lower);

  bool IsBitOp = StringSwitch<bool>(Mnemonic)
                     .Cases({"bset", "bclr", "brset", "brclr"}, true)
                     .Default(false);

  Operands.push_back(HCS08Operand::createToken(Mnemonic, NameLoc));

  unsigned OpNo = 0;
  while (getLexer().isNot(AsmToken::EndOfStatement)) {
    // Separator commas produce no token. A leading comma is how the
    // no-offset indexed modes are written, as in "lda ,x".
    parseOptionalToken(AsmToken::Comma);
    if (getLexer().is(AsmToken::EndOfStatement))
      break;

    if (parseOperand(Operands, IsBitOp && OpNo == 0))
      return true;
    ++OpNo;
  }

  getParser().Lex(); // Consume the EndOfStatement.
  return false;
}

bool HCS08AsmParser::matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                              OperandVector &Operands,
                                              MCStreamer &Out,
                                              uint64_t &ErrorInfo,
                                              bool MatchingInlineAsm) {
  MCInst Inst;
  unsigned Result =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);

  if (Result == Match_Success) {
    Inst.setLoc(IDLoc);
    Out.emitInstruction(Inst, getSTI());
    return false;
  }

  if (Result == Match_MnemonicFail)
    return Error(IDLoc, "invalid instruction mnemonic");

  // Everything else points at a specific operand.
  SMLoc ErrorLoc = IDLoc;
  if (ErrorInfo != ~0ULL) {
    if (ErrorInfo >= Operands.size())
      return Error(IDLoc, "too few operands for instruction");
    ErrorLoc = ((HCS08Operand &)*Operands[ErrorInfo]).getStartLoc();
    if (ErrorLoc == SMLoc())
      ErrorLoc = IDLoc;
  }

  switch (Result) {
  case Match_UImm8:
    return Error(ErrorLoc, "operand must be an 8-bit value");
  case Match_UImm16:
    return Error(ErrorLoc, "operand must be a 16-bit value or a symbol");
  default:
    return Error(ErrorLoc, "invalid operand for instruction");
  }
}

} // end anonymous namespace

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "HCS08GenAsmMatcher.inc"

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeHCS08AsmParser() {
  RegisterMCAsmParser<HCS08AsmParser> X(getTheHCS08Target());
}
