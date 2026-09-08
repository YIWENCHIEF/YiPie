#include "isolation.h"
#include <algorithm>

bool Isolated(const std::string& mode, const std::vector<std::string>& list,
              const std::string& procName) {
    const bool inList = std::find(list.begin(), list.end(), procName) != list.end();
    if (mode == "whitelist") return !inList;   // 不在白名单 -> 隔离
    return inList;                             // 黑名单：在列 -> 隔离
}
