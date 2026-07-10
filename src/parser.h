#pragma once

#include "ast.h"
#include "token.h"

#include <memory>
#include <vector>

namespace toyc {

class Parser {
public:
  explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
  Program parseProgram();

private:
  const Token &peek(int offset = 0) const;
  bool check(TokenKind kind, int offset = 0) const;
  bool match(TokenKind kind);
  Token consume(TokenKind kind, const std::string &message);
  [[noreturn]] void error(const Token &token, const std::string &message) const;

  std::unique_ptr<TopLevel> parseTopLevel();
  Decl parseDecl();
  Function parseFunction();
  Param parseParam();
  std::unique_ptr<BlockStmt> parseBlock();
  std::unique_ptr<Stmt> parseStmt();
  std::unique_ptr<Expr> parseExpr();
  std::unique_ptr<Expr> parseLOr();
  std::unique_ptr<Expr> parseLAnd();
  std::unique_ptr<Expr> parseRel();
  std::unique_ptr<Expr> parseAdd();
  std::unique_ptr<Expr> parseMul();
  std::unique_ptr<Expr> parseUnary();
  std::unique_ptr<Expr> parsePrimary();

  std::vector<Token> tokens_;
  size_t pos_ = 0;
};

} // namespace toyc
