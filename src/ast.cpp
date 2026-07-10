#include "ast.h"

namespace toyc {

const char *typeName(Type type) {
  return type == Type::Int ? "int" : "void";
}

const char *binaryOpName(BinaryOp op) {
  switch (op) {
  case BinaryOp::Add: return "+";
  case BinaryOp::Sub: return "-";
  case BinaryOp::Mul: return "*";
  case BinaryOp::Div: return "/";
  case BinaryOp::Mod: return "%";
  case BinaryOp::Lt: return "<";
  case BinaryOp::Gt: return ">";
  case BinaryOp::Le: return "<=";
  case BinaryOp::Ge: return ">=";
  case BinaryOp::Eq: return "==";
  case BinaryOp::Ne: return "!=";
  case BinaryOp::And: return "&&";
  case BinaryOp::Or: return "||";
  }
  return "?";
}

} // namespace toyc
