// naive_pinyin 内部配置结构
#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

  std::vector<UserWord> user_words;

  // 解析 JSON 配置。解析失败抛 std::runtime_error。
  static Config FromJson(const std::string& json_str);
};

}  // namespace naive_pinyin
