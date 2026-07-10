#include "utils.h"

#include <cstdint>
#include <cctype>
#include <sstream>

namespace toyc {

std::string formatLoc(SourceLoc loc) {
  std::ostringstream os;
  os << loc.line << ":" << loc.col;
  return os.str();
}

std::string sanitizeLabel(const std::string &name) {
  std::string out;
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') out.push_back(c);
    else out.push_back('_');
  }
  return out;
}

int32_t wrap32(int64_t value) {
  return static_cast<int32_t>(static_cast<uint32_t>(value));
}

} // namespace toyc
