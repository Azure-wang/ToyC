#pragma once

#include <cstdint>
#include <string>

namespace toyc {

struct SourceLoc {
  int line = 1;
  int col = 1;
};

enum class TokenKind {
  End,
  Identifier,
  Number,
  KwInt,
  KwVoid,
  KwConst,
  KwIf,
  KwElse,
  KwWhile,
  KwBreak,
  KwContinue,
  KwReturn,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Bang,
  Less,
  Greater,
  LessEq,
  GreaterEq,
  EqEq,
  NotEq,
  AndAnd,
  OrOr,
  Assign,
  LParen,
  RParen,
  LBrace,
  RBrace,
  Comma,
  Semicolon,
};

struct Token {
  TokenKind kind = TokenKind::End;
  std::string text;
  int64_t number = 0;
  SourceLoc loc;
};

std::string tokenKindName(TokenKind kind);

} // namespace toyc
