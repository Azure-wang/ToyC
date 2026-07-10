#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace toyc {

template <class T> class ScopedTable {
public:
  void push() { scopes_.emplace_back(); }
  void pop() { scopes_.pop_back(); }
  bool empty() const { return scopes_.empty(); }

  bool declare(const std::string &name, T value) {
    if (scopes_.empty()) push();
    auto &scope = scopes_.back();
    if (scope.count(name)) return false;
    scope.emplace(name, std::move(value));
    return true;
  }

  T *find(const std::string &name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      auto f = it->find(name);
      if (f != it->end()) return &f->second;
    }
    return nullptr;
  }

  const T *find(const std::string &name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      auto f = it->find(name);
      if (f != it->end()) return &f->second;
    }
    return nullptr;
  }

private:
  std::vector<std::unordered_map<std::string, T>> scopes_;
};

} // namespace toyc
