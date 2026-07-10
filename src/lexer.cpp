#include "lexer.h"

#include "utils.h"

#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace toyc {

std::string tokenKindName(TokenKind kind) {
  switch (kind) {
  case TokenKind::End: return "end of file";
  case TokenKind::Identifier: return "identifier";
  case TokenKind::Number: return "number";
  case TokenKind::KwInt: return "int";
  case TokenKind::KwVoid: return "void";
  case TokenKind::KwConst: return "const";
  case TokenKind::KwIf: return "if";
  case TokenKind::KwElse: return "else";
  case TokenKind::KwWhile: return "while";
  case TokenKind::KwBreak: return "break";
  case TokenKind::KwContinue: return "continue";
  case TokenKind::KwReturn: return "return";
  case TokenKind::Plus: return "+";
  case TokenKind::Minus: return "-";
  case TokenKind::Star: return "*";
  case TokenKind::Slash: return "/";
  case TokenKind::Percent: return "%";
  case TokenKind::Bang: return "!";
  case TokenKind::Less: return "<";
  case TokenKind::Greater: return ">";
  case TokenKind::LessEq: return "<=";
  case TokenKind::GreaterEq: return ">=";
  case TokenKind::EqEq: return "==";
  case TokenKind::NotEq: return "!=";
  case TokenKind::AndAnd: return "&&";
  case TokenKind::OrOr: return "||";
  case TokenKind::Assign: return "=";
  case TokenKind::LParen: return "(";
  case TokenKind::RParen: return ")";
  case TokenKind::LBrace: return "{";
  case TokenKind::RBrace: return "}";
  case TokenKind::Comma: return ",";
  case TokenKind::Semicolon: return ";";
  }
  return "?";
}

char Lexer::peek(int offset) const {
  size_t i = pos_ + static_cast<size_t>(offset);
  if (i >= source_.size()) return '\0';
  return source_[i];
}

char Lexer::get() {
  char c = peek();
  if (c == '\0') return c;
  ++pos_;
  if (c == '\n') {
    ++line_;
    col_ = 1;
  } else {
    ++col_;
  }
  return c;
}

bool Lexer::match(char c) {
  if (peek() != c) return false;
  get();
  return true;
}

Token Lexer::make(TokenKind kind, std::string text, SourceLoc loc) {
  Token t;
  t.kind = kind;
  t.text = std::move(text);
  t.loc = loc;
  return t;
}

void Lexer::error(const std::string &message, SourceLoc loc) const {
  throw CompileError(formatLoc(loc) + ": lexer error: " + message);
}

void Lexer::skipWhitespaceAndComments() {
  while (true) {
    while (std::isspace(static_cast<unsigned char>(peek()))) get();
    if (peek() == '/' && peek(1) == '/') {
      while (peek() != '\0' && peek() != '\n') get();
      continue;
    }
    if (peek() == '/' && peek(1) == '*') {
      SourceLoc loc{line_, col_};
      get();
      get();
      while (!(peek() == '*' && peek(1) == '/')) {
        if (peek() == '\0') error("unterminated block comment", loc);
        get();
      }
      get();
      get();
      continue;
    }
    break;
  }
}

std::vector<Token> Lexer::tokenize() {
  static const std::unordered_map<std::string, TokenKind> keywords = {
      {"int", TokenKind::KwInt},       {"void", TokenKind::KwVoid},
      {"const", TokenKind::KwConst},   {"if", TokenKind::KwIf},
      {"else", TokenKind::KwElse},     {"while", TokenKind::KwWhile},
      {"break", TokenKind::KwBreak},   {"continue", TokenKind::KwContinue},
      {"return", TokenKind::KwReturn},
  };
  std::vector<Token> out;
  while (true) {
    skipWhitespaceAndComments();
    SourceLoc loc{line_, col_};
    char c = peek();
    if (c == '\0') {
      out.push_back(make(TokenKind::End, "", loc));
      return out;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      std::string text;
      while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
        text.push_back(get());
      }
      auto it = keywords.find(text);
      out.push_back(make(it == keywords.end() ? TokenKind::Identifier : it->second, text, loc));
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      std::string text;
      if (c == '0') {
        text.push_back(get());
        if (std::isdigit(static_cast<unsigned char>(peek())))
          error("leading zeros are not allowed in integer literals", loc);
      } else {
        while (std::isdigit(static_cast<unsigned char>(peek()))) text.push_back(get());
      }
      Token t = make(TokenKind::Number, text, loc);
      try {
        t.number = std::stoll(text);
      } catch (...) {
        error("integer literal out of range", loc);
      }
      out.push_back(t);
      continue;
    }

    get();
    switch (c) {
    case '+': out.push_back(make(TokenKind::Plus, "+", loc)); break;
    case '-': out.push_back(make(TokenKind::Minus, "-", loc)); break;
    case '*': out.push_back(make(TokenKind::Star, "*", loc)); break;
    case '/': out.push_back(make(TokenKind::Slash, "/", loc)); break;
    case '%': out.push_back(make(TokenKind::Percent, "%", loc)); break;
    case '(' : out.push_back(make(TokenKind::LParen, "(", loc)); break;
    case ')' : out.push_back(make(TokenKind::RParen, ")", loc)); break;
    case '{' : out.push_back(make(TokenKind::LBrace, "{", loc)); break;
    case '}' : out.push_back(make(TokenKind::RBrace, "}", loc)); break;
    case ',' : out.push_back(make(TokenKind::Comma, ",", loc)); break;
    case ';' : out.push_back(make(TokenKind::Semicolon, ";", loc)); break;
    case '!':
      out.push_back(match('=') ? make(TokenKind::NotEq, "!=", loc) : make(TokenKind::Bang, "!", loc));
      break;
    case '<':
      out.push_back(match('=') ? make(TokenKind::LessEq, "<=", loc) : make(TokenKind::Less, "<", loc));
      break;
    case '>':
      out.push_back(match('=') ? make(TokenKind::GreaterEq, ">=", loc) : make(TokenKind::Greater, ">", loc));
      break;
    case '=':
      out.push_back(match('=') ? make(TokenKind::EqEq, "==", loc) : make(TokenKind::Assign, "=", loc));
      break;
    case '&':
      if (match('&')) out.push_back(make(TokenKind::AndAnd, "&&", loc));
      else error("unexpected '&'; ToyC only supports &&", loc);
      break;
    case '|':
      if (match('|')) out.push_back(make(TokenKind::OrOr, "||", loc));
      else error("unexpected '|'; ToyC only supports ||", loc);
      break;
    default:
      error(std::string("unexpected character '") + c + "'", loc);
    }
  }
}

} // namespace toyc
