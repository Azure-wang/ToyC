#include "parser.h"

#include "utils.h"

#include <sstream>

namespace toyc {

const Token &Parser::peek(int offset) const {
  long long i = static_cast<long long>(pos_) + offset;
  if (i < 0) i = 0;
  if (static_cast<size_t>(i) >= tokens_.size()) return tokens_.back();
  return tokens_[static_cast<size_t>(i)];
}

bool Parser::check(TokenKind kind, int offset) const {
  return peek(offset).kind == kind;
}

bool Parser::match(TokenKind kind) {
  if (!check(kind)) return false;
  ++pos_;
  return true;
}

Token Parser::consume(TokenKind kind, const std::string &message) {
  if (check(kind)) return tokens_[pos_++];
  error(peek(), message + ", got " + tokenKindName(peek().kind));
}

void Parser::error(const Token &token, const std::string &message) const {
  throw CompileError(formatLoc(token.loc) + ": parser error: " + message);
}

Program Parser::parseProgram() {
  Program program;
  while (!check(TokenKind::End)) {
    program.items.push_back(parseTopLevel());
  }
  return program;
}

std::unique_ptr<TopLevel> Parser::parseTopLevel() {
  bool isConst = check(TokenKind::KwConst);
  int typeOffset = isConst ? 1 : 0;
  if (isConst || (check(TokenKind::KwInt) && check(TokenKind::Identifier, 1) &&
                  !check(TokenKind::LParen, 2))) {
    auto loc = peek().loc;
    auto top = std::make_unique<TopDecl>();
    top->loc = loc;
    top->decl = parseDecl();
    return top;
  }
  if ((check(TokenKind::KwInt, typeOffset) || check(TokenKind::KwVoid, typeOffset))) {
    auto loc = peek().loc;
    auto top = std::make_unique<TopFunction>();
    top->loc = loc;
    top->func = parseFunction();
    return top;
  }
  error(peek(), "expected declaration or function definition");
}

Decl Parser::parseDecl() {
  Decl decl;
  decl.loc = peek().loc;
  decl.isConst = match(TokenKind::KwConst);
  consume(TokenKind::KwInt, "expected 'int' in declaration");
  decl.name = consume(TokenKind::Identifier, "expected identifier in declaration").text;
  consume(TokenKind::Assign, "ToyC declarations must have an initializer");
  decl.init = parseExpr();
  consume(TokenKind::Semicolon, "expected ';' after declaration");
  return decl;
}

Function Parser::parseFunction() {
  Function fn;
  fn.loc = peek().loc;
  if (match(TokenKind::KwInt)) fn.returnType = Type::Int;
  else {
    consume(TokenKind::KwVoid, "expected function return type");
    fn.returnType = Type::Void;
  }
  fn.name = consume(TokenKind::Identifier, "expected function name").text;
  consume(TokenKind::LParen, "expected '(' after function name");
  if (!check(TokenKind::RParen)) {
    do {
      fn.params.push_back(parseParam());
    } while (match(TokenKind::Comma));
  }
  consume(TokenKind::RParen, "expected ')' after parameter list");
  fn.body = parseBlock();
  return fn;
}

Param Parser::parseParam() {
  Param p;
  p.loc = peek().loc;
  consume(TokenKind::KwInt, "ToyC only supports int parameters");
  p.name = consume(TokenKind::Identifier, "expected parameter name").text;
  return p;
}

std::unique_ptr<BlockStmt> Parser::parseBlock() {
  SourceLoc loc = consume(TokenKind::LBrace, "expected '{'").loc;
  auto block = std::make_unique<BlockStmt>(loc);
  while (!check(TokenKind::RBrace)) {
    if (check(TokenKind::End)) error(peek(), "unterminated block");
    block->stmts.push_back(parseStmt());
  }
  consume(TokenKind::RBrace, "expected '}'");
  return block;
}

std::unique_ptr<Stmt> Parser::parseStmt() {
  if (check(TokenKind::LBrace)) return parseBlock();
  if (check(TokenKind::KwConst) || check(TokenKind::KwInt)) {
    SourceLoc loc = peek().loc;
    return std::make_unique<DeclStmt>(loc, parseDecl());
  }
  if (match(TokenKind::Semicolon)) return std::make_unique<EmptyStmt>(peek(-1).loc);
  if (match(TokenKind::KwIf)) {
    SourceLoc loc = peek(-1).loc;
    consume(TokenKind::LParen, "expected '(' after if");
    auto cond = parseExpr();
    consume(TokenKind::RParen, "expected ')' after if condition");
    auto thenBranch = parseStmt();
    std::unique_ptr<Stmt> elseBranch;
    if (match(TokenKind::KwElse)) elseBranch = parseStmt();
    return std::make_unique<IfStmt>(loc, std::move(cond), std::move(thenBranch), std::move(elseBranch));
  }
  if (match(TokenKind::KwWhile)) {
    SourceLoc loc = peek(-1).loc;
    consume(TokenKind::LParen, "expected '(' after while");
    auto cond = parseExpr();
    consume(TokenKind::RParen, "expected ')' after while condition");
    auto body = parseStmt();
    return std::make_unique<WhileStmt>(loc, std::move(cond), std::move(body));
  }
  if (match(TokenKind::KwBreak)) {
    SourceLoc loc = peek(-1).loc;
    consume(TokenKind::Semicolon, "expected ';' after break");
    return std::make_unique<BreakStmt>(loc);
  }
  if (match(TokenKind::KwContinue)) {
    SourceLoc loc = peek(-1).loc;
    consume(TokenKind::Semicolon, "expected ';' after continue");
    return std::make_unique<ContinueStmt>(loc);
  }
  if (match(TokenKind::KwReturn)) {
    SourceLoc loc = peek(-1).loc;
    std::unique_ptr<Expr> value;
    if (!check(TokenKind::Semicolon)) value = parseExpr();
    consume(TokenKind::Semicolon, "expected ';' after return");
    return std::make_unique<ReturnStmt>(loc, std::move(value));
  }
  if (check(TokenKind::Identifier) && check(TokenKind::Assign, 1)) {
    SourceLoc loc = peek().loc;
    std::string name = consume(TokenKind::Identifier, "expected identifier").text;
    consume(TokenKind::Assign, "expected '='");
    auto value = parseExpr();
    consume(TokenKind::Semicolon, "expected ';' after assignment");
    return std::make_unique<AssignStmt>(loc, std::move(name), std::move(value));
  }
  SourceLoc loc = peek().loc;
  auto expr = parseExpr();
  consume(TokenKind::Semicolon, "expected ';' after expression");
  return std::make_unique<ExprStmt>(loc, std::move(expr));
}

std::unique_ptr<Expr> Parser::parseExpr() { return parseLOr(); }

std::unique_ptr<Expr> Parser::parseLOr() {
  auto lhs = parseLAnd();
  while (match(TokenKind::OrOr)) {
    SourceLoc loc = peek(-1).loc;
    lhs = std::make_unique<BinaryExpr>(loc, BinaryOp::Or, std::move(lhs), parseLAnd());
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseLAnd() {
  auto lhs = parseRel();
  while (match(TokenKind::AndAnd)) {
    SourceLoc loc = peek(-1).loc;
    lhs = std::make_unique<BinaryExpr>(loc, BinaryOp::And, std::move(lhs), parseRel());
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseRel() {
  auto lhs = parseAdd();
  while (true) {
    BinaryOp op;
    SourceLoc loc = peek().loc;
    if (match(TokenKind::Less)) op = BinaryOp::Lt;
    else if (match(TokenKind::Greater)) op = BinaryOp::Gt;
    else if (match(TokenKind::LessEq)) op = BinaryOp::Le;
    else if (match(TokenKind::GreaterEq)) op = BinaryOp::Ge;
    else if (match(TokenKind::EqEq)) op = BinaryOp::Eq;
    else if (match(TokenKind::NotEq)) op = BinaryOp::Ne;
    else break;
    lhs = std::make_unique<BinaryExpr>(loc, op, std::move(lhs), parseAdd());
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseAdd() {
  auto lhs = parseMul();
  while (true) {
    BinaryOp op;
    SourceLoc loc = peek().loc;
    if (match(TokenKind::Plus)) op = BinaryOp::Add;
    else if (match(TokenKind::Minus)) op = BinaryOp::Sub;
    else break;
    lhs = std::make_unique<BinaryExpr>(loc, op, std::move(lhs), parseMul());
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseMul() {
  auto lhs = parseUnary();
  while (true) {
    BinaryOp op;
    SourceLoc loc = peek().loc;
    if (match(TokenKind::Star)) op = BinaryOp::Mul;
    else if (match(TokenKind::Slash)) op = BinaryOp::Div;
    else if (match(TokenKind::Percent)) op = BinaryOp::Mod;
    else break;
    lhs = std::make_unique<BinaryExpr>(loc, op, std::move(lhs), parseUnary());
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseUnary() {
  SourceLoc loc = peek().loc;
  if (match(TokenKind::Plus)) return std::make_unique<UnaryExpr>(loc, UnaryOp::Plus, parseUnary());
  if (match(TokenKind::Minus)) return std::make_unique<UnaryExpr>(loc, UnaryOp::Minus, parseUnary());
  if (match(TokenKind::Bang)) return std::make_unique<UnaryExpr>(loc, UnaryOp::Not, parseUnary());
  return parsePrimary();
}

std::unique_ptr<Expr> Parser::parsePrimary() {
  if (match(TokenKind::Number)) {
    const Token &tok = peek(-1);
    return std::make_unique<NumberExpr>(tok.loc, wrap32(tok.number));
  }
  if (match(TokenKind::Identifier)) {
    const Token &tok = peek(-1);
    if (match(TokenKind::LParen)) {
      std::vector<std::unique_ptr<Expr>> args;
      if (!check(TokenKind::RParen)) {
        do {
          args.push_back(parseExpr());
        } while (match(TokenKind::Comma));
      }
      consume(TokenKind::RParen, "expected ')' after call arguments");
      return std::make_unique<CallExpr>(tok.loc, tok.text, std::move(args));
    }
    return std::make_unique<VarExpr>(tok.loc, tok.text);
  }
  if (match(TokenKind::LParen)) {
    auto expr = parseExpr();
    consume(TokenKind::RParen, "expected ')' after expression");
    return expr;
  }
  error(peek(), "expected expression");
}

} // namespace toyc
