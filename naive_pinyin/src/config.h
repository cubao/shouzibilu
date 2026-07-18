// naive_pinyin 内部配置结构
#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "user_freq.h"

namespace naive_pinyin {

struct UserWord {
  std::string pinyin;  // 空格分隔的全拼音节，如 "na yi ge"
  std::string word;
  double freq = 0;
};

struct Config {
  // 双拼映射：2 字母编码 -> 全拼音节。空表 = 全拼模式。
  std::unordered_map<std::string, std::string> shuangpin_map;

  // 模糊音对：(from, to)，如 ("z", "zh") 表示输入 z 时也匹配 zh。
  std::vector<std::pair<std::string, std::string>> fuzzy;

  int max_candidates = 10;

  // DP 打分：每多一个分段的罚分。越大越偏好长词/少分段。
  int segment_penalty = 1100;

  std::vector<UserWord> user_words;

  // 动态调频：加分参数与持久化叠加层（np_dump_user 的导出回传）。
  UserBoostParams user_boost;
  std::vector<UserFreqEntry> user_freq;

  // 解析 JSON 配置。解析失败抛 std::runtime_error。
  static Config FromJson(const std::string& json_str);
};

}  // namespace naive_pinyin
