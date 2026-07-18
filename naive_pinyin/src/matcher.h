// DP 整句匹配：音节 DAG × 词典 trie → 候选
#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config.h"
#include "dict.h"
#include "syllable.h"
#include "user_freq.h"

namespace naive_pinyin {

struct MatchCandidate {
  std::string text;
  int consumed = 0;   // 消耗掉的 raw input 字符数（含 apostrophe）
  double score = 0;
  // 分段信息：(拼音key, 词)，供提交调频/自造词检测。
  std::vector<std::pair<std::string, std::string>> segments;
};

class Matcher {
 public:
  // segment_penalty: 每多一个分段扣的分。越大越偏好长词/少分段。
  // shuangpin_map 非空时按双拼切分（2 字母一码查表），否则全拼。
  // user_freq 非空时应用动态调频加分。
  Matcher(const Dict& dict, std::vector<std::pair<std::string, std::string>> fuzzy,
          int max_candidates, int segment_penalty,
          const std::unordered_map<std::string, std::string>* shuangpin_map = nullptr,
          const UserFreq* user_freq = nullptr);

  std::vector<MatchCandidate> Match(const std::string& input) const;

  // 生成音节的模糊音变体（含原音节）。
  std::vector<std::string> Variants(const std::string& syllable) const;

 private:
  const Dict& dict_;
  int max_candidates_;
  int segment_penalty_;
  const std::unordered_map<std::string, std::string>* shuangpin_map_;
  const UserFreq* user_freq_;

  // 模糊音规则：声母对（如 z<->zh）与韵母对（如 in<->ing）。
  std::vector<std::pair<std::string, std::string>> initial_pairs_;
  std::vector<std::pair<std::string, std::string>> final_pairs_;
};

}  // namespace naive_pinyin
