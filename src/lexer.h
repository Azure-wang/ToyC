#pragma once

#include "token.h"

#include <string>
#include <vector>

namespace toyc {

class Lexer {
public:
  explicit Lexer(std::string source) : source_(std::move(source)) {}
  std::vector<Token> tokenize();

private:
  char peek(int offset = 0) const;
  char get();
  bool match(char c);
  void skipWhitespaceAndComments();
  Token make(TokenKind kind, std::string text, SourceLoc loc);
  [[noreturn]] void error(const std::string &message, SourceLoc loc) const;

  std::string source_;
  size_t pos_ = 0;
  int line_ = 1;
  int col_ = 1;
};

} // namespace toyc
