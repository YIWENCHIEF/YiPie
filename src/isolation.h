#pragma once
// M3d T1: 进程隔离谓词（纯逻辑，无 Win32，可单测）。
// 返回 true = 该进程被隔离（不触发轮盘起手）。
// mode 非 "whitelist" 一律按黑名单语义。procName 须为小写基名（调用方保证）。
#include <string>
#include <vector>

bool Isolated(const std::string& mode, const std::vector<std::string>& list,
              const std::string& procName);
