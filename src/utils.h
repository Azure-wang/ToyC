#pragma once

#include "token.h"

#include <stdexcept>
#include <string>

namespace toyc {

class CompileError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

std::string formatLoc(SourceLoc loc);
std::string sanitizeLabel(const std::string &name);
int32_t wrap32(int64_t value);

} // namespace toyc
